#pragma once

#include <algorithm>

#include <core/Utils.h>
#include <willpower/common/Vector2.h>

namespace bw::app {

// Pitch is held short of straight up and down so the view never flips over.
constexpr float PitchLimit = 85.0f;

inline float applyMouseYaw(float yaw, float mouseDeltaX, float sensitivity) {
  // Authored yaw is clockwise in the world plane, matching rightward mouse
  // motion now that world +Y maps to renderer -Z.
  return core::clamp_angle(yaw + mouseDeltaX * sensitivity);
}

inline float applyMousePitch(float pitch, float mouseDeltaY, float sensitivity) {
  return std::clamp(pitch + mouseDeltaY * sensitivity, -PitchLimit, PitchLimit);
}

inline wp::Vector2 playerMovement(wp::Vector2 input, float yaw) {
  input.rotateClockwise(yaw);
  return input;
}

inline float worldViewAngle(float playerYaw) {
  return core::clamp_angle(playerYaw);
}

inline float cameraYaw(float playerYaw) {
  // Renderer yaw zero looks along -Z, which is authored world +Y.
  return core::clamp_angle(playerYaw);
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
  // than merely negating, as the latter would place the whole overlay
  // off-screen whenever viewOffset is its world-space lower-left corner.
  //
  // World +X remains screen-right, matching the editor and the 3D renderer's
  // handedness-preserving world +Y -> renderer -Z mapping.
  return {
      (worldPosition.x - viewOffset.x) * viewScale.x,
      viewSize.y - (worldPosition.y - viewOffset.y) * viewScale.y};
}

}  // namespace bw::app
