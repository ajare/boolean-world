#pragma once

#include <cmath>
#include <cstdint>

#include <core/ArrangementWorldData.h>

#include <common/GameDefines.h>

#include "PlayerSurfaceTraversal.h"

namespace bw::app {

// Pitch is positive while looking down (see FpsCamera::updateAngles).
[[nodiscard]] inline bool isLookingUpForLiquidClimb(float playerPitch) {
  return playerPitch < 0.0f;
}

[[nodiscard]] inline bool isFacingLiquidClimbTarget(
    float forwardDotTargetDirection) {
  return forwardDotTargetDirection > 0.0f;
}

[[nodiscard]] inline bool maySuppressOverlappingTallStep(bool swimming) {
  // Swimmers must leave liquid only through the checked climb-out action.
  return !swimming;
}

[[nodiscard]] inline bool isDescendingForTallStepTraversal(
    bool swimming, float verticalVelocity) {
  // The descending exemption lets a dry player fall from a high floor without
  // having its step wall reappear behind them. A swimmer moving downward has
  // not crossed from that high floor and must remain blocked by the bank.
  return !swimming && verticalVelocity < 0.0f;
}

[[nodiscard]] inline bool canClimbOutOfLiquidToFloor(
    float playerFeetElevation, float targetFloorElevation) {
  if (targetFloorElevation <= playerFeetElevation) {
    return false;
  }

  auto eyeElevation = playerFeetElevation + BW_PLAYER_EYE_HEIGHT;
  return std::abs(targetFloorElevation - eyeElevation) <=
         BW_PLAYER_MANTLE_WATER;
}

// Validates every landing-dependent part of a water mantle at the proposed
// player-centre position. Sampling a known face first keeps an Arrangement-edge
// destination unambiguous; the final containment query still proves that the
// proposal actually landed in that face.
[[nodiscard]] inline bool canLandLiquidMantle(
    bw::core::ArrangementWorldData const& world,
    uint32_t targetFace,
    wp::Vector2 const& sourcePosition,
    wp::Vector2 const& landingPosition,
    float playerFeetElevation) {
  auto surface = world.getSurfaceSample(targetFace, landingPosition);
  if (!surface ||
      !canClimbOutOfLiquidToFloor(
          playerFeetElevation, surface->floorElevation) ||
      !isPlayerFloorWalkable(surface->floorNormal) ||
      surface->ceilingElevation - surface->floorElevation <
          BW_PLAYER_HEIGHT) {
    return false;
  }

  return world.getContainingFaceIndex(landingPosition) == targetFace &&
         world.circleIntersectsWallForTraversal(
             landingPosition, BW_PLAYER_RADIUS, sourcePosition) < 0;
}

}  // namespace bw::app
