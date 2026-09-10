#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

#include "common/GameDefines.h"

namespace {
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

constexpr int contourVertexCount = 32;
// Fixed-point units are 1000 per world unit (Arrangement.h), so a radius of
// 100000 here is 100 world units - large enough for a sensible World extent
// and grid cell size.
constexpr int64_t worldUnitInFixedPoint = 1000;

Contour circleContour(int64_t radiusWorldUnits) {
  Contour contour;
  contour.reserve(contourVertexCount);
  auto radius = radiusWorldUnits * worldUnitInFixedPoint;
  for (int i = 0; i < contourVertexCount; ++i) {
    auto angle = 2.0 * std::numbers::pi * double(i) /
                 double(contourVertexCount);
    contour.push_back({int64_t(std::llround(double(radius) * std::cos(angle))),
                       int64_t(std::llround(double(radius) * std::sin(angle)))});
  }
  return contour;
}

PrimitivePropertySet propertiesWithHeights(float floorZ, float ceilingZ) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

// An outer annulus (floorZ 0, ceilingZ 48 - a tall, roomy area) with a disc
// nested in its hole, the disc's height range set by the caller. Mirrors
// arrangement_leaf_boundary_tests.cpp's nested annulus/disc fixture.
std::vector<ArrangementPrimitive> annulusAndDisc(
    PrimitivePropertySet const& discProperties) {
  return {{{circleContour(100), circleContour(60)},
           Primitive::Operation::Union,
           Primitive::FillRule::EvenOdd,
           0,
           10,
           propertiesWithHeights(0.0f, 48.0f)},
          {{circleContour(60)},
           Primitive::Operation::Union,
           Primitive::FillRule::EvenOdd,
           1,
           11,
           discProperties}};
}

void wallClearanceReflectsTheSharedHeadroomBetweenBothFaces() {
  // A floor step alone (ceilings equal): clearance uses both floors against
  // the shared ceiling.
  {
    auto arrangement = bw::core::arr::BuildArrangement(
        annulusAndDisc(propertiesWithHeights(20.0f, 48.0f)));
    auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
    bool found = false;
    for (auto const& wall : walls) {
      if (wall.kind == ArrangementWallKind::FloorStep) {
        found = true;
        require(std::abs(wall.clearance - 28.0f) < 0.01f,
                "a floor-only step's clearance did not use both floors against the shared ceiling");
      }
    }
    require(found, "a differing floorZ did not produce a FloorStep wall");
  }

  // A ceiling step alone (floors equal): clearance uses both ceilings
  // against the shared floor.
  {
    auto arrangement = bw::core::arr::BuildArrangement(
        annulusAndDisc(propertiesWithHeights(0.0f, 24.0f)));
    auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
    bool found = false;
    for (auto const& wall : walls) {
      if (wall.kind == ArrangementWallKind::CeilingStep) {
        found = true;
        require(std::abs(wall.clearance - 24.0f) < 0.01f,
                "a ceiling-only step's clearance did not use both ceilings against the shared floor");
      }
    }
    require(found, "a differing ceilingZ did not produce a CeilingStep wall");
  }

  // Both floor and ceiling differ: every wall on the shared boundary reports
  // the same overlap of the two faces' full height ranges.
  {
    auto arrangement = bw::core::arr::BuildArrangement(
        annulusAndDisc(propertiesWithHeights(12.0f, 24.0f)));
    auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
    bool sawFloorStep = false, sawCeilingStep = false;
    for (auto const& wall : walls) {
      if (wall.kind == ArrangementWallKind::Border) {
        continue;
      }
      require(std::abs(wall.clearance - 12.0f) < 0.01f,
              "a wall between two differently-sized faces reported the wrong clearance");
      sawFloorStep |= wall.kind == ArrangementWallKind::FloorStep;
      sawCeilingStep |= wall.kind == ArrangementWallKind::CeilingStep;
    }
    require(sawFloorStep && sawCeilingStep,
            "a shell differing in both floorZ and ceilingZ did not produce both wall kinds");
  }
}

void stepWallsUseMaterialFromTheOccludingFace() {
  auto discProperties = propertiesWithHeights(12.0f, 24.0f);
  discProperties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("disc.wall");
  auto arrangement = bw::core::arr::BuildArrangement(
      annulusAndDisc(discProperties));
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  bool sawFloorStep = false;
  bool sawCeilingStep = false;
  for (auto const& wall : walls) {
    if (wall.kind != ArrangementWallKind::FloorStep &&
        wall.kind != ArrangementWallKind::CeilingStep) {
      continue;
    }
    require(arrangement->palette[wall.paletteIndex].wallMaterial == bw::core::SurfaceMaterialReference::subMaterial("disc.wall"),
            "a step wall did not use the occluding face's wall material");
    sawFloorStep |= wall.kind == ArrangementWallKind::FloorStep;
    sawCeilingStep |= wall.kind == ArrangementWallKind::CeilingStep;
  }
  require(sawFloorStep && sawCeilingStep,
          "the material fixture did not produce both step wall kinds");
}

void insufficientClearanceBlocksMovementRegardlessOfWallKind() {
  wp::BoundingBox extents({-150.0f, -150.0f}, {300.0f, 300.0f});
  constexpr float gridCellSize = 20.0f;
  // The disc boundary is a regular 32-gon of radius 60 world units; vertex 0
  // sits exactly at (60, 0).
  wp::Vector2 boundaryPoint{60.0f, 0.0f};

  {
    // floorZ 12, ceilingZ 24: only 12 units of headroom against the annulus's
    // 0/48 range - less than BW_PLAYER_HEIGHT, so this boundary must block
    // even though it is entirely a CeilingStep/FloorStep pair, neither of
    // which the step-threshold rule alone would ever refuse.
    require(24.0f - 12.0f < BW_PLAYER_HEIGHT,
            "test fixture assumption drifted: the pocket no longer has less than a player's height of clearance");
    auto arrangement = bw::core::arr::BuildArrangement(
        annulusAndDisc(propertiesWithHeights(12.0f, 24.0f)));
    bw::core::ArrangementWorldData data(
        arrangement, extents, gridCellSize);
    auto nearby = data.getWallsNear(boundaryPoint, 2.0f);
    require(!nearby.empty(),
            "a boundary with less than the player's height in shared clearance did not block");
  }

  {
    // floorZ 4, ceilingZ 40: 36 units of headroom - comfortably walkable.
    require(40.0f - 4.0f >= BW_PLAYER_HEIGHT,
            "test fixture assumption drifted: the room no longer has at least a player's height of clearance");
    auto arrangement = bw::core::arr::BuildArrangement(
        annulusAndDisc(propertiesWithHeights(4.0f, 40.0f)));
    bw::core::ArrangementWorldData data(
        arrangement, extents, gridCellSize);
    auto nearby = data.getWallsNear(boundaryPoint, 2.0f);
    require(nearby.empty(),
            "a boundary with ample shared clearance was blocked");
  }
}

}  // namespace

int main() {
  try {
    wallClearanceReflectsTheSharedHeadroomBetweenBothFaces();
    stepWallsUseMaterialFromTheOccludingFace();
    insufficientClearanceBlocksMovementRegardlessOfWallKind();
    std::cout << "Arrangement walls block on insufficient shared clearance, not just wall kind\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
