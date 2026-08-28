#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/PrimitivePropertySet.h>

namespace {
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::WallNormalMapOverride;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;

constexpr int64_t U = bw::core::arr::FixedPointUnitsPerWorldUnit;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

Contour rectangle(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

ArrangementPrimitive square(
    uint64_t priority, uint32_t id, PrimitivePropertySet properties = {}) {
  return {{rectangle(0, 0, 10 * U, 10 * U)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, priority, id, properties};
}

void normalOnEdge(
    ArrangementPrimitive& primitive, size_t edge,
    WallNormalMapOverride const& value) {
  primitive.contourEdgeNormalMapOverrides.resize(1);
  primitive.contourEdgeNormalMapOverrides[0].resize(4);
  primitive.contourEdgeNormalMapOverrides[0][edge] = value;
}

std::vector<bw::core::arr::ArrangementWall> wallsFor(
    std::vector<ArrangementPrimitive> primitives) {
  return bw::core::arr::BuildArrangementWalls(
      *bw::core::arr::BuildArrangement(primitives));
}

size_t countNormalMap(
    std::vector<bw::core::arr::ArrangementWall> const& walls,
    WallNormalMapOverride const& value) {
  return std::count_if(walls.begin(), walls.end(), [&](auto const& wall) {
    return wall.normalMapOverride == value;
  });
}

void triStateAndFoldPrecedenceAreExplicit() {
  auto image = WallNormalMapOverride::image("normal/low.png", 4.0f, 0.25f);
  auto highImage = WallNormalMapOverride::image("normal/high.png", 8.0f, 0.8f);
  auto low = square(10, 1);
  normalOnEdge(low, 0, image);
  auto highUnset = square(20, 2);
  // Deliberately put an explicit Unset in the input: it is not a winning
  // choice and must not clear the lower Image.
  normalOnEdge(highUnset, 0, WallNormalMapOverride::unset());
  auto highDisabled = square(20, 2);
  normalOnEdge(highDisabled, 0, WallNormalMapOverride::disabled());
  auto highMapped = square(20, 2);
  normalOnEdge(highMapped, 0, highImage);

  for (auto primitives : {std::vector<ArrangementPrimitive>{highUnset, low},
                          std::vector<ArrangementPrimitive>{low, highUnset}}) {
    auto walls = wallsFor(std::move(primitives));
    require(countNormalMap(walls, image) == 1,
            "Unset did not preserve the lower-precedence Image independent of input order");
  }
  auto disabledWalls = wallsFor({low, highDisabled});
  require(countNormalMap(disabledWalls, WallNormalMapOverride::disabled()) == 1,
          "Disabled did not explicitly suppress a lower Image");
  auto mappedWalls = wallsFor({highMapped, low});
  require(countNormalMap(mappedWalls, highImage) == 1,
          "the highest-precedence Image did not win as one complete value");

  auto structural = highMapped;
  structural.contributesProperties = false;
  auto structuralWalls = wallsFor({low, structural});
  require(countNormalMap(structuralWalls, image) == 1,
          "a property-transparent Primitive selected a wall normal-map value");
}

void overridesReachSplitBordersAndBothStepKinds() {
  auto image = WallNormalMapOverride::image("normal/split.png", 2.0f, 0.5f);
  auto base = square(0, 1);
  normalOnEdge(base, 2, image);  // top edge, split by the notch below.
  auto notch = square(1, 2);
  notch.contours[0] = rectangle(4 * U, 8 * U, 6 * U, 12 * U);
  auto splitWalls = wallsFor({base, notch});
  size_t mappedBorders = 0;
  for (auto const& wall : splitWalls) {
    mappedBorders += wall.kind == ArrangementWallKind::Border &&
                     wall.normalMapOverride == image;
  }
  require(mappedBorders == 2,
          "a split source edge did not propagate its Image to every surviving sub-segment");

  PrimitivePropertySet lowProperties;
  lowProperties.floorZ = 0;
  lowProperties.ceilingZ = 48;
  PrimitivePropertySet highProperties;
  highProperties.floorZ = 8;
  highProperties.ceilingZ = 40;
  auto left = square(0, 1, lowProperties);
  left.contours[0] = rectangle(0, 0, 10 * U, 10 * U);
  normalOnEdge(left, 1, image);
  auto right = square(1, 2, highProperties);
  right.contours[0] = rectangle(10 * U, 0, 20 * U, 10 * U);
  auto steps = wallsFor({left, right});
  bool floor = false, ceiling = false;
  for (auto const& wall : steps) {
    if (wall.kind == ArrangementWallKind::FloorStep) {
      floor = wall.normalMapOverride == image;
    }
    if (wall.kind == ArrangementWallKind::CeilingStep) {
      ceiling = wall.normalMapOverride == image;
    }
  }
  require(floor && ceiling,
          "FloorStep and CeilingStep walls did not inherit their Arrangement edge's Image");
}

void erasedEdgesDoNotCreateWalls() {
  auto image = WallNormalMapOverride::image("normal/erased.png", 1.0f, 1.0f);
  auto left = square(0, 1);
  normalOnEdge(left, 1, image);
  auto right = square(1, 2);
  right.contours[0] = rectangle(10 * U, 0, 20 * U, 10 * U);
  auto walls = wallsFor({left, right});
  require(std::none_of(walls.begin(), walls.end(), [&](auto const& wall) {
            return wall.normalMapOverride == image;
          }),
          "an override was represented on an edge that the fold erased");
}
}  // namespace

int main() {
  try {
    triStateAndFoldPrecedenceAreExplicit();
    overridesReachSplitBordersAndBothStepKinds();
    erasedEdgesDoNotCreateWalls();
    std::cout << "Wall normal-map overrides resolve through the arrangement fold\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
