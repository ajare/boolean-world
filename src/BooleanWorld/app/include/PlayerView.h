#pragma once

#include <core/Utils.h>
#include <willpower/common/Vector2.h>

namespace bw::app {

inline float applyMouseYaw(float yaw, float mouseDeltaX) {
  return core::clamp_angle(yaw + mouseDeltaX);
}

inline wp::Vector2 playerMovement(wp::Vector2 input, float yaw) {
  // Camera yaw zero looks along world -Y. Keep local forward/right aligned
  // with the camera's horizontal forward/right axes.
  input.y = -input.y;
  input.rotateAnticlockwise(yaw);
  return input;
}

inline float worldViewAngle(float playerYaw) {
  // Core view angles are clockwise from +Y; player yaw is clockwise from -Y.
  return core::clamp_angle(180.0f - playerYaw);
}

inline float minimapRadius(float worldRadius, wp::Vector2 const& viewScale) {
  return worldRadius * viewScale.x;
}

inline wp::Vector2 minimapPosition(
    wp::Vector2 const& worldPosition,
    wp::Vector2 const& viewOffset,
    wp::Vector2 const& viewScale) {
  // World Y maps to render Z. Keeping its sign here makes camera-forward
  // appear upward and camera-right appear rightward on the minimap.
  return {
      (worldPosition.x - viewOffset.x) * viewScale.x,
      (worldPosition.y - viewOffset.y) * viewScale.y};
}

}  // namespace bw::app
