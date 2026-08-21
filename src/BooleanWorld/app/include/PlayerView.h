#pragma once

#include <algorithm>

#include <core/Utils.h>
#include <willpower/common/Vector2.h>

namespace bw::app {

// Pitch is held short of straight up and down so the view never flips over.
constexpr float PitchLimit = 85.0f;

inline float applyMouseYaw(float yaw, float mouseDeltaX, float sensitivity) {
  return core::clamp_angle(yaw + mouseDeltaX * sensitivity);
}

inline float applyMousePitch(float pitch, float mouseDeltaY, float sensitivity) {
  return std::clamp(pitch + mouseDeltaY * sensitivity, -PitchLimit, PitchLimit);
}

inline wp::Vector2 playerMovement(wp::Vector2 input, float yaw) {
  // Player and core view angles are clockwise from world +Y.
  input.rotateClockwise(yaw);
  return input;
}

inline float worldViewAngle(float playerYaw) {
  return core::clamp_angle(playerYaw);
}

inline float cameraYaw(float playerYaw) {
  // The renderer's camera yaw is clockwise from world -Y.
  return core::clamp_angle(180.0f - playerYaw);
}

inline float minimapRadius(float worldRadius, wp::Vector2 const& viewScale) {
  return worldRadius * viewScale.x;
}

inline wp::Vector2 minimapPosition(
    wp::Vector2 const& worldPosition,
    wp::Vector2 const& viewOffset,
    wp::Vector2 const& viewScale) {
  // Screen Y points down, so negate world Y to keep camera-forward upward
  // and camera-right rightward on the minimap at zero yaw.
  return {
      (worldPosition.x - viewOffset.x) * viewScale.x,
      -(worldPosition.y - viewOffset.y) * viewScale.y};
}

}  // namespace bw::app
