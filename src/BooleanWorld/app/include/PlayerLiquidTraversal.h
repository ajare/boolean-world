#pragma once

#include <cmath>

#include <common/GameDefines.h>

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
    float playerFloorZ, float targetFloorZ) {
  if (targetFloorZ <= playerFloorZ) {
    return false;
  }

  auto eyeZ = playerFloorZ + BW_PLAYER_EYE_HEIGHT;
  return std::abs(targetFloorZ - eyeZ) <= BW_PLAYER_MAX_CLIMB_OUT_HEIGHT;
}

}  // namespace bw::app
