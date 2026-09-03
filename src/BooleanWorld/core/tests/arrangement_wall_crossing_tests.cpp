#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

#include "common/GameDefines.h"

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(std::optional<float> value, float expected) {
  return value && std::abs(*value - expected) < 0.05f;
}

constexpr int contourVertexCount = 32;
// Fixed-point units are 1000 per world unit (Arrangement.h).
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

// An outer annulus (0..48) with a disc nested in its hole, the disc's height
// range set by the caller - the same shape arrangement_wall_clearance_tests
// uses. Equal heights leave the r=60 boundary with no wall at all; a raised
// floor or a lowered ceiling turns it into a step of exactly that height.
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

ArrangementWorldData discSnapshot(PrimitivePropertySet const& discProperties) {
  return ArrangementWorldData(
      bw::core::arr::BuildArrangement(annulusAndDisc(discProperties)),
      wp::BoundingBox({-150.0f, -150.0f}, {300.0f, 300.0f}),
      20.0f);
}

// A ray along a contour edge's midpoint rather than one of its vertices, so
// each crossing is an unambiguous edge hit. For a regular n-gon whose first
// vertex sits at angle zero, that bearing is pi/n and the crossing distance
// from the centre is the apothem.
double const midEdgeAngle = std::numbers::pi / double(contourVertexCount);
float const apothemFactor = float(std::cos(midEdgeAngle));
wp::Vector2 const outward{float(std::cos(midEdgeAngle)),
                          float(std::sin(midEdgeAngle))};

// The ray runs inward from the annulus, across the disc, and back out into
// the annulus on the far side, so one call sees the near boundary, the far
// boundary, and the outer Border behind them both.
wp::Vector2 const rayStart = outward * 80.0f;
wp::Vector2 const rayEnd = outward * -110.0f;
float const nearBoundary = 80.0f - 60.0f * apothemFactor;
float const farBorder = 80.0f + 100.0f * apothemFactor;

void aBorderBlocksTheRayAtEveryHeightItSpans() {
  // No step at r=60: the ray crosses the disc boundary unhindered and is
  // stopped only by the outer Border, from either end of the 0..48 range.
  auto data = discSnapshot(propertiesWithHeights(0.0f, 48.0f));
  require(
      near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 10.0f), farBorder),
      "a low ray was not carried across an unstepped boundary to the Border");
  require(
      near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 30.0f), farBorder),
      "a high ray was not carried across an unstepped boundary to the Border");
}

void aFloorStepBlocksOnlyBelowItsTop() {
  // Disc floor raised to 20: a FloorStep spanning 0..20 at r=60.
  auto data = discSnapshot(propertiesWithHeights(20.0f, 48.0f));
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 10.0f),
               nearBoundary),
          "a ray below the top of a floor step was not stopped by it");
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 30.0f),
               farBorder),
          "a ray above a floor step did not pass over it to the Border");
}

void aCeilingStepBlocksOnlyAboveItsBottom() {
  // Disc ceiling lowered to 24: a CeilingStep spanning 24..48 at r=60.
  auto data = discSnapshot(propertiesWithHeights(0.0f, 24.0f));
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 30.0f),
               nearBoundary),
          "a ray above the bottom of a ceiling step was not stopped by it");
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 10.0f),
               farBorder),
          "a ray below a ceiling step did not pass under it to the Border");
}

void aStepPairLeavesTheGapBetweenThemOpen() {
  // Disc 12..24 against the annulus's 0..48 raises a FloorStep spanning
  // 0..12 and drops a CeilingStep spanning 24..48, with the 12..24 pocket
  // between them open. Only a ray inside that pocket gets through.
  auto data = discSnapshot(propertiesWithHeights(12.0f, 24.0f));
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 6.0f),
               nearBoundary),
          "a ray below a step pair's floor step was not stopped by it");
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 30.0f),
               nearBoundary),
          "a ray above a step pair's ceiling step was not stopped by it");
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 18.0f),
               farBorder),
          "a ray through the open pocket between two steps was blocked");
  // The step edges themselves are drawn surfaces, so a ray level with either
  // lip is grazing geometry and stops rather than slipping between them.
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 12.0f),
               nearBoundary),
          "a ray level with the top of a floor step passed through it");
  require(near(data.distanceToFirstWallCrossing(rayStart, rayEnd, 24.0f),
               nearBoundary),
          "a ray level with the bottom of a ceiling step passed through it");
}

void aRayThatFallsShortIsUnobstructed() {
  auto data = discSnapshot(propertiesWithHeights(20.0f, 48.0f));
  // Stops 10 units into the annulus, well before the step at r=60.
  auto shortEnd = outward * 70.0f;
  require(!data.distanceToFirstWallCrossing(rayStart, shortEnd, 10.0f),
          "a ray ending short of every wall reported a crossing");
  require(!data.distanceToFirstWallCrossing(rayStart, rayStart, 10.0f),
          "a zero-length ray reported a crossing");
}

Contour rectContour(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

void anUndrawnWallBlocksNothing() {
  // Sight and light are stopped by what a wall draws, not by whether it
  // collides: a Border hidden by an authored override still blocks movement
  // but must not shorten a ray.
  auto room = [](std::optional<bool> rightEdgeVisible) {
    constexpr int64_t U = worldUnitInFixedPoint;
    std::vector<std::optional<bool>> visibleOverrides(4, std::nullopt);
    // contour[1] = (100,0) -> contour[2] = (100,100): the right-hand edge.
    visibleOverrides[1] = rightEdgeVisible;
    ArrangementPrimitive square{
        {rectContour(0, 0, 100 * U, 100 * U)},
        Primitive::Operation::Union,
        Primitive::FillRule::EvenOdd,
        0,
        1,
        propertiesWithHeights(0.0f, 48.0f),
        {},
        {visibleOverrides}};
    return ArrangementWorldData(
        bw::core::arr::BuildArrangement({square}),
        wp::BoundingBox({-50.0f, -50.0f}, {200.0f, 200.0f}),
        20.0f);
  };

  wp::Vector2 from{50.0f, 50.0f};
  wp::Vector2 to{150.0f, 50.0f};
  require(near(room(std::nullopt).distanceToFirstWallCrossing(from, to, 24.0f),
               50.0f),
          "a drawn Border did not stop the ray at the wall (fixture broken)");
  require(!room(false).distanceToFirstWallCrossing(from, to, 24.0f),
          "a wall the World does not draw still blocked the ray");
}
}  // namespace

int main() {
  try {
    aBorderBlocksTheRayAtEveryHeightItSpans();
    aFloorStepBlocksOnlyBelowItsTop();
    aCeilingStepBlocksOnlyAboveItsBottom();
    aStepPairLeavesTheGapBetweenThemOpen();
    aRayThatFallsShortIsUnobstructed();
    anUndrawnWallBlocksNothing();
  } catch (std::exception const& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }
  return 0;
}
