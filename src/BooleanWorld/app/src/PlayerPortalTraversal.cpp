#include "PlayerPortalTraversal.h"

#include <algorithm>
#include <cmath>

#include <core/ArrangementWorldData.h>
#include <core/Portal.h>

namespace bw::app {
namespace {
constexpr float PlaneTolerance = 1.0e-5f;
constexpr float BoundsTolerance = 0.001f;
constexpr float RepeatedStateTolerance = 0.001f;

PortalEndpointIdentity Identity(
    core::ResolvedPortalPair const& pair, uint32_t endpoint) {
  return {pair.layerId, pair.pairId, static_cast<uint8_t>(endpoint)};
}

bool SameRepeatedState(
    PortalRepeatedState const& a, PortalRepeatedState const& b) {
  return a.endpoint == b.endpoint &&
         a.crossingPosition.distanceTo(b.crossingPosition) <=
             RepeatedStateTolerance &&
         a.unconsumedMovement.distanceTo(b.unconsumedMovement) <=
             RepeatedStateTolerance;
}
}  // namespace

PlayerPortalCrossingResult tryPlayerPortalCrossing(
    core::ArrangementWorldData const& world,
    core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpoint,
    float playerRadius,
    float playerHeight,
    PlayerPortalMotion& motion,
    PlayerPortalUpdateState& updateState) {
  if (!pair.active || sourceEndpoint >= pair.endpoints.size()) {
    return PlayerPortalCrossingResult::NotCrossing;
  }
  auto const& source = pair.endpoints[sourceEndpoint].aperture;
  auto const& destination = pair.endpoints[1u - sourceEndpoint].aperture;
  auto startDistance = (motion.position - source.centre).dot(source.front);
  auto endPosition = motion.position + motion.unconsumedMovement;
  auto endDistance = (endPosition - source.centre).dot(source.front);
  if (startDistance <= PlaneTolerance || endDistance >= -PlaneTolerance) {
    return PlayerPortalCrossingResult::NotCrossing;
  }
  auto denominator = startDistance - endDistance;
  if (denominator <= PlaneTolerance) {
    return PlayerPortalCrossingResult::NotCrossing;
  }
  auto fraction = startDistance / denominator;
  if (fraction < 0.0f || fraction > 1.0f) {
    return PlayerPortalCrossingResult::NotCrossing;
  }

  auto crossing =
      motion.position + motion.unconsumedMovement * fraction;
  auto tangentDistance = std::abs((crossing - source.centre).dot(source.tangent));
  auto horizontalLimit = source.width * 0.5f - playerRadius;
  auto verticalFits =
      motion.feetElevation >= source.bottom - BoundsTolerance &&
      motion.feetElevation + playerHeight <= source.top + BoundsTolerance;
  if (horizontalLimit < 0.0f ||
      tangentDistance > horizontalLimit + BoundsTolerance || !verticalFits) {
    return PlayerPortalCrossingResult::Blocked;
  }

  auto identity = Identity(pair, sourceEndpoint);
  if (updateState.exitSide.active &&
      updateState.exitSide.endpoint == identity) {
    return PlayerPortalCrossingResult::Blocked;
  }
  auto remaining = motion.unconsumedMovement * (1.0f - fraction);
  PortalRepeatedState repeated{identity, crossing, remaining};
  if (updateState.crossings >= MaxPortalCrossingsPerUpdate ||
      std::ranges::any_of(
          updateState.visited,
          [&](auto const& previous) {
            return SameRepeatedState(previous, repeated);
          })) {
    updateState.terminatedByBudgetOrRepeat = true;
    return PlayerPortalCrossingResult::Blocked;
  }

  auto transform = core::BuildPortalRigidTransform(pair, sourceEndpoint);
  auto transformedCrossing = transform.transformPoint(crossing);
  auto destinationPosition =
      transformedCrossing + destination.front * PortalExitPlaneEpsilon;
  auto destinationFeet =
      transform.transformElevation(motion.feetElevation);
  auto destinationSurface = world.getSurfaceSample(destinationPosition);
  if (!destinationSurface ||
      destinationFeet <
          destinationSurface->floorElevation - BoundsTolerance ||
      destinationFeet + playerHeight >
          destinationSurface->ceilingElevation + BoundsTolerance ||
      world.circleIntersectsWallForTraversal(
          destinationPosition, playerRadius, destinationPosition, false) >= 0) {
    return PlayerPortalCrossingResult::Blocked;
  }

  // Liquid is not intrinsically solid: a valid destination may intentionally
  // put the player into a swimmable column. Still evaluate its local state in
  // the crossing frame and reject corrupt/non-finite clearance rather than
  // publishing location-dependent state that disagrees with the destination.
  auto liquidDepth = world.getLiquidDepth(destinationPosition);
  auto destinationClearance = destinationSurface->ceilingElevation -
                              destinationSurface->floorElevation;
  if (!std::isfinite(liquidDepth) || liquidDepth < 0.0f ||
      liquidDepth > destinationClearance + BoundsTolerance ||
      destinationClearance + BoundsTolerance < playerHeight) {
    return PlayerPortalCrossingResult::Blocked;
  }

  motion.position = destinationPosition;
  motion.feetElevation = destinationFeet;
  motion.yaw = transform.transformYaw(motion.yaw);
  motion.horizontalVelocity =
      transform.transformVector(motion.horizontalVelocity);
  motion.unconsumedMovement = transform.transformVector(remaining);
  // pitch and verticalVelocity deliberately remain untouched; the transform
  // preserves world-up and gravity remains the ordinary vertical simulation.

  updateState.visited.push_back(repeated);
  ++updateState.crossings;
  updateState.exitSide = {
      Identity(pair, 1u - sourceEndpoint), true};
  updateState.cameraCut = true;
  return PlayerPortalCrossingResult::Traversed;
}

void updatePortalExitSideState(
    core::ArrangementWorldData const& world,
    wp::Vector2 const& playerPosition,
    float playerRadius,
    PortalExitSideState& state) {
  if (!state.active) return;
  auto const* pair = world.findPortalPair(
      state.endpoint.layerId, state.endpoint.pairId);
  if (!pair || !pair->active || state.endpoint.endpoint >= 2) {
    state = {};
    return;
  }
  auto const& aperture =
      pair->endpoints[state.endpoint.endpoint].aperture;
  auto frontDistance =
      (playerPosition - aperture.centre).dot(aperture.front);
  if (frontDistance >= playerRadius + PortalExitPlaneEpsilon) {
    state = {};
  }
}

}  // namespace bw::app
