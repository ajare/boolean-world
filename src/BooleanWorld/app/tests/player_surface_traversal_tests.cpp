#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/PrimitivePropertySet.h>

#include <common/GameDefines.h>

#include "PlayerSurfaceTraversal.h"
#include "PlayerVerticalPhysics.h"

namespace {
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;

constexpr int64_t U = 1000;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, float tolerance,
                 std::string const& message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(
        message + ": expected " + std::to_string(expected) + ", got " +
        std::to_string(actual));
  }
}

Contour rectangle(int x0, int y0, int x1, int y1) {
  return {{x0 * U, y0 * U},
          {x1 * U, y0 * U},
          {x1 * U, y1 * U},
          {x0 * U, y1 * U}};
}

PrimitivePropertySet surfaces(
    float floor, float ceiling, wp::Vector2 floorGradient = {},
    wp::Vector2 ceilingGradient = {}) {
  PrimitivePropertySet result;
  result.floorZ = floor;
  result.floorZ.gradient = floorGradient;
  result.ceilingZ = ceiling;
  result.ceilingZ.gradient = ceilingGradient;
  return result;
}

ArrangementPrimitive region(
    int x0, int x1, uint32_t primitiveIndex,
    PrimitivePropertySet const& properties) {
  return {{rectangle(x0, 0, x1, 10)},
          Primitive::Operation::Union,
          Primitive::FillRule::NonZero,
          primitiveIndex,
          primitiveIndex,
          properties};
}

std::shared_ptr<bw::core::ArrangementWorldData> dataFor(
    std::vector<ArrangementPrimitive> primitives) {
  auto arrangement = bw::core::arr::BuildArrangement(primitives);
  return std::make_shared<bw::core::ArrangementWorldData>(
      arrangement, wp::BoundingBox({-5.0f, -5.0f}, {50.0f, 20.0f}), 8.0f);
}

void groundedPlayerFollowsOneAscendingPlane() {
  auto data = dataFor({region(0, 20, 1, surfaces(0.0f, 40.0f, {1.0f, 0.0f}))});
  auto traversal = bw::app::evaluatePlayerSurfaceTraversal(
      *data, {1.0f, 5.0f}, {9.0f, 5.0f}, 1.0f, 0.0f);

  require(!traversal.clearanceBlocked,
          "a roomy ascending plane was blocked");
  require(traversal.supportedFloorElevation.has_value(),
          "an ascending continuous plane lost grounded support");
  requireNear(*traversal.supportedFloorElevation, 9.0f, 0.001f,
              "grounded ascent was treated as repeated vertical steps");
}

void continuousDescentFollowsOnlyWhileGrounded() {
  auto data = dataFor({region(0, 20, 1, surfaces(0.0f, 40.0f, {1.0f, 0.0f}))});
  auto grounded = bw::app::evaluatePlayerSurfaceTraversal(
      *data, {9.0f, 5.0f}, {1.0f, 5.0f}, 9.0f, 0.0f);
  require(grounded.supportedFloorElevation.has_value(),
          "a grounded player lost a continuous descending plane");
  requireNear(*grounded.supportedFloorElevation, 1.0f, 0.001f,
              "a grounded player did not follow the descending plane");

  auto airborne = bw::app::evaluatePlayerSurfaceTraversal(
      *data, {9.0f, 5.0f}, {1.0f, 5.0f}, 12.0f, -3.0f);
  require(!airborne.supportedFloorElevation,
          "a descending floor pulled an airborne player downward");
}

void losingSupportEntersOrdinaryVerticalPhysics() {
  auto data = dataFor(
      {region(0, 10, 1, surfaces(10.0f, 40.0f)),
       region(10, 20, 2, surfaces(0.0f, 40.0f))});
  auto traversal = bw::app::evaluatePlayerSurfaceTraversal(
      *data, {9.0f, 5.0f}, {11.0f, 5.0f}, 10.0f, 0.0f);
  require(!traversal.supportedFloorElevation,
          "a lower discontinuous floor retained grounded support");

  bw::app::PlayerVerticalInputs inputs;
  inputs.floorElevation = 0.0f;
  inputs.frameTime = 0.1f;
  auto fallen = bw::app::stepPlayerVerticalPhysics({10.0f, 0.0f}, inputs);
  require(fallen.verticalVelocity < 0.0f && fallen.feetElevation < 10.0f &&
              fallen.feetElevation > inputs.floorElevation,
          "loss of support did not enter ordinary falling physics");
}

void crossedArrangementEdgesAreReportedInMovementOrder() {
  auto data = dataFor(
      {region(0, 10, 1, surfaces(0.0f, 40.0f)),
       region(10, 20, 2, surfaces(12.0f, 40.0f)),
       region(20, 30, 3, surfaces(24.0f, 48.0f))});
  auto traversal = data->getSurfaceTraversal({5.0f, 5.0f}, {25.0f, 5.0f});

  require(traversal.size() == 3,
          "movement was not split at both crossed Arrangement edges");
  requireNear(traversal[0].endFraction, 0.25f, 0.001f,
              "first Arrangement edge was not processed first");
  requireNear(traversal[1].endFraction, 0.75f, 0.001f,
              "second Arrangement edge was not processed second");
  require(traversal[0].endSurface && traversal[1].beginSurface &&
              traversal[1].endSurface && traversal[2].beginSurface,
          "crossing surfaces were not sampled in their explicit faces");
  requireNear(traversal[0].endSurface->floorElevation, 0.0f, 0.001f,
              "first crossing used the wrong source floor");
  requireNear(traversal[1].beginSurface->floorElevation, 12.0f, 0.001f,
              "first crossing used the wrong target floor");
  requireNear(traversal[1].endSurface->floorElevation, 12.0f, 0.001f,
              "second crossing did not use the locally current face");

  auto blockingWalls = data->getWallsNearForTraversal(
      {25.0f, 5.0f}, 2.0f, {5.0f, 5.0f});
  std::erase_if(blockingWalls, [&](uint32_t index) {
    return data->getWalls()[index].kind != ArrangementWallKind::FloorStep;
  });
  require(blockingWalls.size() == 2,
          "both locally tall crossed floor Steps were not applied");
  auto crossingX = [&](uint32_t wallIndex) {
    auto const& arrangement = data->getArrangement();
    auto const& edge = arrangement.edges[data->getWalls()[wallIndex].edge];
    return bw::core::arr::ToWorldCoordinate(
        arrangement.vertices[edge.v[0]].x);
  };
  requireNear(crossingX(blockingWalls[0]), 10.0f, 0.001f,
              "horizontal traversal did not apply the first Step first");
  requireNear(crossingX(blockingWalls[1]), 20.0f, 0.001f,
              "horizontal traversal did not apply the second Step second");
}

void laterStepsUseTheFaceReachedAtEarlierCrossings() {
  auto first = region(0, 10, 1, surfaces(0.0f, 50.0f));
  auto middle = region(10, 20, 2, surfaces(4.0f, 50.0f));
  auto last = region(
      20, 30, 3, surfaces(-10.0f, 50.0f, {0.0f, 2.0f}));
  middle.contourEdgeOverrides = {
      {std::nullopt, false, std::nullopt, false}};
  auto data = dataFor({first, middle, last});

  auto walls = data->getWallsNearForTraversal(
      {25.0f, 5.0f}, 2.0f, {5.0f, 5.0f});
  auto blockedByFloorStep = std::ranges::any_of(walls, [&](uint32_t index) {
    return data->getWalls()[index].kind == ArrangementWallKind::FloorStep;
  });
  require(!blockedByFloorStep,
          "a later descent was evaluated from the movement's original face");

  auto approachingWalls = data->getWallsNearForTraversal(
      {19.0f, 5.0f}, 2.0f, {5.0f, 5.0f});
  auto approachBlocked =
      std::ranges::any_of(approachingWalls, [&](uint32_t index) {
        return data->getWalls()[index].kind == ArrangementWallKind::FloorStep;
      });
  require(!approachBlocked,
          "a nearby later descent ignored the face reached before it");
}

void variableStepEdgesUseTheirLocalCrossingHeight() {
  auto left = region(
      0, 10, 1, surfaces(0.0f, 40.0f, {0.0f, 0.5f}));
  auto right = region(
      10, 20, 2, surfaces(10.0f, 40.0f, {0.0f, -0.5f}));
  // Keep the generated Step default open where its local rise is walkable.
  left.contourEdgeOverrides = {{std::nullopt, false}};
  auto data = dataFor({left, right});

  auto includesFloorStep = [&](float y) {
    auto walls = data->getWallsNearForTraversal(
        {11.0f, y}, 2.0f, {9.0f, y});
    return std::ranges::any_of(walls, [&](uint32_t index) {
      return data->getWalls()[index].kind == ArrangementWallKind::FloorStep;
    });
  };
  require(includesFloorStep(0.0f),
          "a locally tall part of a variable Step edge did not block");
  require(!includesFloorStep(4.0f),
          "a locally walkable part of a variable Step edge was blocked");
  require(!includesFloorStep(8.0f),
          "a local descent on a variable Step edge was blocked");
}

void slopedCeilingIsCheckedAcrossEveryAffineSegment() {
  auto data = dataFor(
      {region(0, 10, 1, surfaces(0.0f, 30.0f)),
       region(10, 20, 2, surfaces(0.0f, 39.0f, {}, {-1.0f, 0.0f}))});
  auto traversal = bw::app::evaluatePlayerSurfaceTraversal(
      *data, {5.0f, 5.0f}, {20.0f, 5.0f}, 0.0f, 0.0f);

  // The second face has clearance 29 at x=10 and 19 at x=20. It reaches the
  // player's height at x=19, after the Arrangement crossing at x=10.
  require(traversal.clearanceBlocked,
          "a passage narrowing below player height remained traversable");
  requireNear(traversal.allowedFraction, 14.0f / 15.0f, 0.001f,
              "sloped-ceiling clearance was not solved on its local segment");
}
}  // namespace

int main() {
  try {
    groundedPlayerFollowsOneAscendingPlane();
    continuousDescentFollowsOnlyWhileGrounded();
    losingSupportEntersOrdinaryVerticalPhysics();
    crossedArrangementEdgesAreReportedInMovementOrder();
    laterStepsUseTheFaceReachedAtEarlierCrossings();
    variableStepEdgesUseTheirLocalCrossingHeight();
    slopedCeilingIsCheckedAcrossEveryAffineSegment();
    std::cout << "Player surface traversal follows local affine geometry\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
