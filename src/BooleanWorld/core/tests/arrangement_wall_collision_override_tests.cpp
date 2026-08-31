#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

#include "common/GameDefines.h"

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
  // CCW winding, matching the convention used elsewhere in this test suite
  // (e.g. arrangement_same_face_edge_tests.cpp).
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

// Finds the arrangement edge index whose endpoints match world-space points
// a/b (in either direction). Returns ~0u if not found.
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

// 1. A collides = false override on a Border-producing edge opens up what
//    would otherwise block.
void falseOverrideOpensUpABorderWall() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  // Bottom edge: contour[0] = (0,0) -> contour[1] = (10,0).
  overrides[0] = false;

  ArrangementPrimitive square{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {overrides}};

  auto arrangement = bw::core::arr::BuildArrangement({square});
  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);

  // The overridden bottom edge must no longer block.
  require(data.circleIntersectsWall({5.0f, 0.0f}, 0.5f) == -1,
          "a collides = false override did not open up an otherwise-blocking Border wall");

  // An unmodified edge (the right side) must still block as before.
  require(data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) != -1,
          "an edge without an override stopped blocking (test fixture broken)");
}

// 2. A collides = false override cannot make a floor step taller than the
//    player's maximum step height traversable upward, but the same boundary
//    remains traversable downward from the upper face.
void maximumStepHeightAppliesOnlyWhenAscending() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  overrides[1] = false;  // right edge of the lower square

  ArrangementPrimitive lower{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {overrides}};
  ArrangementPrimitive upper{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(12.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({lower, upper});
  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);
  auto ascending = data.getWallsNearForTraversal(
      {10.0f, 5.0f}, 0.5f, {5.0f, 5.0f});
  auto descending = data.getWallsNearForTraversal(
      {10.0f, 5.0f}, 0.5f, {15.0f, 5.0f});
  auto fallingOverLowerFace = data.getWallsNearForTraversal(
      {10.0f, 5.0f}, 0.5f, {5.0f, 5.0f}, true);
  auto includesFloorStep = [&](
                               bw::core::ArrangementWorldData const& worldData,
                               std::vector<uint32_t> const& wallIndices) {
    auto const& worldWalls = worldData.getWalls();
    return std::any_of(
        wallIndices.begin(), wallIndices.end(), [&](uint32_t wallIndex) {
          return worldWalls[wallIndex].kind == ArrangementWallKind::FloorStep;
        });
  };

  upper.properties.floorZ = BW_PLAYER_STEP_HEIGHT;
  auto atLimitArrangement =
      bw::core::arr::BuildArrangement({lower, upper});
  bw::core::ArrangementWorldData atLimitData(
      atLimitArrangement, extents, gridCellSize);
  auto atLimitAscending = atLimitData.getWallsNearForTraversal(
      {10.0f, 5.0f}, 0.5f, {5.0f, 5.0f});
  require(!includesFloorStep(atLimitData, atLimitAscending),
          "a floor at the player's fixed step height was blocked");
  require(includesFloorStep(data, ascending),
          "collides = false bypassed the maximum step height while ascending");
  require(!includesFloorStep(data, descending),
          "the maximum step height blocked traversal from the upper floor");
  require(!includesFloorStep(data, fallingOverLowerFace),
          "a tall step wall trapped an actor already falling over its lower face");
}

void falseOverrideDoesNotBypassInsufficientClearance() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  overrides[1] = false;
  ArrangementPrimitive left{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {overrides}};
  ArrangementPrimitive right{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(4.0f, 16.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({left, right});
  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);
  require(data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) != -1,
          "collides = false bypassed insufficient Step-wall clearance");
}

// 3. A collides = true override forces a Step wall to block even when its
//    height is below the player's step height and its clearance exceeds player
//    height.
void trueOverrideForcesAStepWallToBlock() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  // Left/right squares sharing the boundary at x = 10. floorZ differs by
  // only 4 (under BW_PLAYER_STEP_HEIGHT) and clearance is ample (32, well
  // over BW_PLAYER_HEIGHT) - absent the override, this boundary must not block.
  auto leftFloorZ = 4.0f;
  auto rightFloorZ = 8.0f;
  auto ceilingZ = 40.0f;
  require(rightFloorZ - leftFloorZ < BW_PLAYER_STEP_HEIGHT,
          "test fixture assumption drifted: the step height must stay below the player's capability");
  require(ceilingZ - rightFloorZ >= BW_PLAYER_HEIGHT,
          "test fixture assumption drifted: clearance must stay ample");

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  // Right edge of the left square: contour[1] = (10,0) -> contour[2] = (10,10).
  overrides[1] = true;

  ArrangementPrimitive left{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(leftFloorZ, ceilingZ),
      {overrides}};
  ArrangementPrimitive right{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(rightFloorZ, ceilingZ)};

  auto arrangement = bw::core::arr::BuildArrangement({left, right});
  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);

  require(data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) != -1,
          "a collides = true override did not force a below-step-height, ample-clearance Step wall to block");
}

// 4. An edge whose wall gets split into multiple arrangement sub-segments by
//    another Primitive crossing it has every resulting sub-segment respect
//    the override.
void overrideSurvivesSplittingIntoSubSegments() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  // Top edge: contour[2] = (10,10) -> contour[3] = (0,10).
  overrides[2] = false;

  ArrangementPrimitive square{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {overrides}};

  // A notch that crosses the top edge between x = 4 and x = 6, splitting it
  // into two remaining wall sub-segments: [10,10]-[6,10] and [4,10]-[0,10].
  ArrangementPrimitive notch{
      {rectContour(4 * U, 8 * U, 6 * U, 12 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(0.0f, 48.0f)};

  auto arrangement = bw::core::arr::BuildArrangement({square, notch});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  // Confirm the notch actually split the original top edge into distinct
  // sub-segments (otherwise this test is not exercising the split path at
  // all): the two outer sub-segments should each still be Border walls
  // (the notch is Unioned at the same height, so it does not erase them).
  auto rightSubSegment = findArrangementEdge(arrangement, {10.0f, 10.0f}, {6.0f, 10.0f});
  auto leftRemainder = findArrangementEdge(arrangement, {4.0f, 10.0f}, {0.0f, 10.0f});
  require(rightSubSegment != ~0u && leftRemainder != ~0u,
          "the split top edge's two surviving sub-segments were not found");
  auto isWallEdge = [&](uint32_t edgeIndex) {
    return std::any_of(walls.begin(), walls.end(), [&](auto const& wall) {
      return wall.edge == edgeIndex;
    });
  };
  require(isWallEdge(rightSubSegment) && isWallEdge(leftRemainder),
          "the split top edge's surviving sub-segments were not produced as ArrangementWalls");

  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);

  require(data.circleIntersectsWall({8.0f, 10.0f}, 0.4f) == -1,
          "the override was not respected on one sub-segment of the split edge");
  require(data.circleIntersectsWall({2.0f, 10.0f}, 0.4f) == -1,
          "the override was not respected on the other sub-segment of the split edge");

  // An edge untouched by both override and split (bottom) still blocks.
  require(data.circleIntersectsWall({5.0f, 0.0f}, 0.5f) != -1,
          "an unrelated, unmodified edge stopped blocking (test fixture broken)");
}

// 5. Two Primitives' edges coinciding exactly - one carrying an override,
//    one without - resolve to the override value regardless of ordering.
void coincidingEdgesResolveToTheOverrideRegardlessOfOrder() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  // Absent the override, this boundary would not block (small step, ample
  // clearance) - see trueOverrideForcesAStepWallToBlock above.
  auto leftFloorZ = 4.0f;
  auto rightFloorZ = 8.0f;
  auto ceilingZ = 40.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  overrides[1] = true;  // right edge of the left square

  ArrangementPrimitive overridden{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(leftFloorZ, ceilingZ),
      {overrides}};
  ArrangementPrimitive plain{
      {rectContour(10 * U, 0, 20 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      2,
      propertiesWithHeights(rightFloorZ, ceilingZ)};

  for (bool overriddenFirst : {true, false}) {
    std::vector<ArrangementPrimitive> primitives = overriddenFirst
        ? std::vector<ArrangementPrimitive>{overridden, plain}
        : std::vector<ArrangementPrimitive>{plain, overridden};
    auto arrangement = bw::core::arr::BuildArrangement(primitives);
    bw::core::ArrangementWorldData data(
        arrangement, extents, gridCellSize);
    require(data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) != -1,
            overriddenFirst
                ? "the Mesh-sourced override did not win when listed first"
                : "the Mesh-sourced override did not win when listed second");
  }
}

// 6. Coincident authored overrides combine conservatively: false dominates
//    true, while two true overrides remain true. Primitive order must not
//    matter.
void coincidentMeshEdgeOverridesResolveFalseFirst() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  for (auto authoredValues :
       {std::pair{true, true}, std::pair{true, false}}) {
    std::vector<std::optional<bool>> leftOverrides(4, std::nullopt);
    leftOverrides[1] = authoredValues.first;  // right edge
    std::vector<std::optional<bool>> rightOverrides(4, std::nullopt);
    rightOverrides[3] = authoredValues.second;  // left edge

    ArrangementPrimitive left{
        {rectContour(0, 0, 10 * U, 10 * U)},
        Primitive::Operation::Union,
        Primitive::FillRule::EvenOdd,
        0,
        1,
        propertiesWithHeights(3.0f, 48.0f),
        {leftOverrides}};
    ArrangementPrimitive right{
        {rectContour(10 * U, 0, 20 * U, 10 * U)},
        Primitive::Operation::Union,
        Primitive::FillRule::EvenOdd,
        0,
        2,
        propertiesWithHeights(6.0f, 48.0f),
        {rightOverrides}};

    for (bool leftFirst : {true, false}) {
      std::vector<ArrangementPrimitive> primitives = leftFirst
          ? std::vector<ArrangementPrimitive>{left, right}
          : std::vector<ArrangementPrimitive>{right, left};
      auto arrangement = bw::core::arr::BuildArrangement(primitives);
      bw::core::ArrangementWorldData data(
          arrangement, extents, gridCellSize);
      auto collides =
          data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) != -1;
      require(collides == (authoredValues.first && authoredValues.second),
              "coincident Mesh edge overrides did not resolve false first");
    }
  }
}

// 7. A collides = true override on an edge whose wall the fold completely
//    erases (two contours Unioned flat with matching floorZ/ceilingZ) has no
//    effect: no wall is produced.
void overrideHasNoEffectWhenTheFoldErasesTheWall() {
  wp::BoundingBox extents({-5.0f, -5.0f}, {30.0f, 30.0f});
  constexpr float gridCellSize = 20.0f;

  std::vector<std::optional<bool>> overrides(4, std::nullopt);
  overrides[1] = true;  // right edge of the left square

  ArrangementPrimitive left{
      {rectContour(0, 0, 10 * U, 10 * U)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      1,
      propertiesWithHeights(0.0f, 48.0f),
      {overrides}};
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
          "a flat Union with matching floor/ceiling produced a wall despite a collides = true override");

  bw::core::ArrangementWorldData data(
      arrangement, extents, gridCellSize);
  require(data.circleIntersectsWall({10.0f, 5.0f}, 0.5f) == -1,
          "a collides = true override synthesized a wall the fold did not produce");
}

}  // namespace

int main() {
  try {
    falseOverrideOpensUpABorderWall();
    maximumStepHeightAppliesOnlyWhenAscending();
    falseOverrideDoesNotBypassInsufficientClearance();
    trueOverrideForcesAStepWallToBlock();
    overrideSurvivesSplittingIntoSubSegments();
    coincidingEdgesResolveToTheOverrideRegardlessOfOrder();
    coincidentMeshEdgeOverridesResolveFalseFirst();
    overrideHasNoEffectWhenTheFoldErasesTheWall();
    std::cout << "The fold propagates the mesh edge collision override into wall collision\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
