#pragma once

#include <algorithm>

#include <core/Utils.h>
#include <willpower/common/Vector2.h>

namespace bw::app {

// Pitch is held short of straight up and down so the view never flips over.
constexpr float PitchLimit = 85.0f;

inline float applyMouseYaw(float yaw, float mouseDeltaX, float sensitivity) {
  // Player yaw is clockwise in the 2D world, but the renderer views the X/Z
  // plane with the opposite handedness. Rightward mouse motion must therefore
  // reduce the authored yaw.
  return core::clamp_angle(yaw - mouseDeltaX * sensitivity);
}

inline float applyMousePitch(float pitch, float mouseDeltaY, float sensitivity) {
  return std::clamp(pitch + mouseDeltaY * sensitivity, -PitchLimit, PitchLimit);
}

inline wp::Vector2 playerMovement(wp::Vector2 input, float yaw) {
  // Mapping world (X,Y) to renderer (X,Z) reverses the camera's horizontal
  // basis: at zero yaw its screen-right direction is world -X.
  input.x = -input.x;
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
    wp::Vector2 const& viewSize,
    wp::Vector2 const& viewScale) {
  // Screen Y points down. Reflect around the bottom of the viewport rather
  // than merely negating Y: the latter places the entire overlay above the
  // screen whenever viewOffset is its world-space lower-left corner.
  return {
      (worldPosition.x - viewOffset.x) * viewScale.x,
      viewSize.y - (worldPosition.y - viewOffset.y) * viewScale.y};
}

}  // namespace bw::app
