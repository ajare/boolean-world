#include <algorithm>
#include <cstdint>
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
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementResultPtr;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;
using bw::core::arr::FixedPointVertex;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Fixed-point units are 1000 per world unit (Arrangement.h).
constexpr int64_t U = 1000;

Contour rectContour(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

wp::Vector2 toWorld(FixedPointVertex const& v) {
  return {float(double(v.x) / double(U)), float(double(v.y) / double(U))};
}

PrimitivePropertySet propertiesWithHeights(float floorZ, float ceilingZ) {
  PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

uint32_t findArrangementEdge(
    ArrangementResultPtr const& arrangement,
    wp::Vector2 const& a,
    wp::Vector2 const& b) {
  for (uint32_t i = 0; i < uint32_t(arrangement->edges.size()); ++i) {
    auto const& edge = arrangement->edges[i];
    auto p0 = toWorld(arrangement->vertices[edge.v[0]]);
    auto p1 = toWorld(arrangement->vertices[edge.v[1]]);
    auto close = [](wp::Vector2 const& x, wp::Vector2 const& y) {
      return std::abs(x.x - y.x) < 0.01f && std::abs(x.y - y.y) < 0.01f;
    };
    if ((close(p0, a) && close(p1, b)) || (close(p0, b) && close(p1, a))) {
      return i;
    }
  }
  return ~0u;
}

bool wallVisible(
    std::vector<bw::core::arr::ArrangementWall> const& walls, uint32_t edgeIndex) {
  auto found = std::find_if(walls.begin(), walls.end(), [&](auto const& wall) {
    return wall.edge == edgeIndex;
  });
  require(found != walls.end(), "expected a wall at the given edge but none was produced");
  return found->visible;
}

// 1. Borders default visible, matching the "defaulting to true" requirement.
void bordersDefaultVisible() {
  ArrangementPrimitive square{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({square});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  require(!walls.empty(), "the square fixture produced no walls at all");
  for (auto const& wall : walls) {
    require(wall.visible, "a wall with no authored override did not default to visible = true");
  }
}

// 2. A visible = false override hides an otherwise-rendered Border wall.
void falseOverrideHidesABorderWall() {
  std::vector<std::optional<bool>> visibleOverrides(4, std::nullopt);
  // Bottom edge: contour[0] = (0,0) -> contour[1] = (10,0).
  visibleOverrides[0] = false;

  ArrangementPrimitive square{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {},
      {visibleOverrides}};

  auto arrangement = bw::core::arr::BuildArrangement({square});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  auto bottomEdge = findArrangementEdge(arrangement, {0.0f, 0.0f}, {10.0f, 0.0f});
  require(bottomEdge != ~0u, "the bottom edge could not be located in the arrangement");
  require(!wallVisible(walls, bottomEdge),
          "a visible = false override did not hide an otherwise-rendered Border wall");

  auto rightEdge = findArrangementEdge(arrangement, {10.0f, 0.0f}, {10.0f, 10.0f});
  require(rightEdge != ~0u, "the right edge could not be located in the arrangement");
  require(wallVisible(walls, rightEdge),
          "an edge without an override stopped rendering (test fixture broken)");
}

// 3. An edge whose wall gets split into multiple arrangement sub-segments has
//    every resulting sub-segment respect the override.
void overrideSurvivesSplittingIntoSubSegments() {
  std::vector<std::optional<bool>> visibleOverrides(4, std::nullopt);
  // Top edge: contour[2] = (10,10) -> contour[3] = (0,10).
  visibleOverrides[2] = false;

  ArrangementPrimitive square{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {},
      {visibleOverrides}};

  ArrangementPrimitive notch{
      {rectContour(4 * U, 8 * U, 6 * U, 12 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({square, notch});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  auto rightSubSegment = findArrangementEdge(arrangement, {10.0f, 10.0f}, {6.0f, 10.0f});
  auto leftRemainder = findArrangementEdge(arrangement, {4.0f, 10.0f}, {0.0f, 10.0f});
  require(rightSubSegment != ~0u && leftRemainder != ~0u,
          "the split top edge's two surviving sub-segments were not found");

  require(!wallVisible(walls, rightSubSegment),
          "the override was not respected on one sub-segment of the split edge");
  require(!wallVisible(walls, leftRemainder),
          "the override was not respected on the other sub-segment of the split edge");

  auto bottomEdge = findArrangementEdge(arrangement, {0.0f, 0.0f}, {10.0f, 0.0f});
  require(bottomEdge != ~0u, "the bottom edge could not be located in the arrangement");
  require(wallVisible(walls, bottomEdge),
          "an unrelated, unmodified edge stopped rendering (test fixture broken)");
}

// 4. Two Primitives' edges coinciding exactly - one carrying an override, one
//    without - resolve to the override value regardless of ordering.
void coincidingEdgesResolveToTheOverrideRegardlessOfOrder() {
  std::vector<std::optional<bool>> visibleOverrides(4, std::nullopt);
  visibleOverrides[1] = false;  // right edge of the left square

  ArrangementPrimitive overridden{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {},
      {visibleOverrides}};
  ArrangementPrimitive plain{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(4.0f, 40.0f)};

  for (bool overriddenFirst : {true, false}) {
    std::vector<ArrangementPrimitive> primitives = overriddenFirst
        ? std::vector<ArrangementPrimitive>{overridden, plain}
        : std::vector<ArrangementPrimitive>{plain, overridden};
    auto arrangement = bw::core::arr::BuildArrangement(primitives);
    auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
    auto sharedEdge = findArrangementEdge(arrangement, {10.0f, 0.0f}, {10.0f, 10.0f});
    require(sharedEdge != ~0u, "the shared boundary edge could not be located");
    require(!wallVisible(walls, sharedEdge),
            overriddenFirst
                ? "the Mesh-sourced override did not win when listed first"
                : "the Mesh-sourced override did not win when listed second");
  }
}

// 5. A visible = false override on an edge whose wall the fold completely
//    erases has no effect: no wall is produced at all (not a hidden one).
void overrideHasNoEffectWhenTheFoldErasesTheWall() {
  std::vector<std::optional<bool>> visibleOverrides(4, std::nullopt);
  visibleOverrides[1] = false;  // right edge of the left square

  ArrangementPrimitive left{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {},
      {visibleOverrides}};
  ArrangementPrimitive right{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({left, right});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  auto sharedEdgeIndex = findArrangementEdge(arrangement, {10.0f, 0.0f}, {10.0f, 10.0f});
  require(sharedEdgeIndex != ~0u,
          "the shared boundary edge could not be located in the arrangement");
  auto wallAtSharedEdge = std::find_if(
      walls.begin(), walls.end(),
      [&](auto const& wall) { return wall.edge == sharedEdgeIndex; });
  require(wallAtSharedEdge == walls.end(),
          "a flat Union with matching floor/ceiling produced a wall despite a visible = false override");
}

}  // namespace

int main() {
  try {
    bordersDefaultVisible();
    falseOverrideHidesABorderWall();
    overrideSurvivesSplittingIntoSubSegments();
    coincidingEdgesResolveToTheOverrideRegardlessOfOrder();
    overrideHasNoEffectWhenTheFoldErasesTheWall();
    std::cout << "The fold propagates the mesh edge visibility override into wall rendering\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
