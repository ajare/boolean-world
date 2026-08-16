#pragma once

#include <algorithm>
#include <cmath>

#include "core/Utils.h"

namespace bw {
namespace core {
using namespace std;

float clamp_angle(float angle) {
  if (!isfinite(angle)) {
    return 0.0f;
  }

  angle = fmod(angle, 360.0f);
  return angle < 0.0f ? angle + 360.0f : angle;
}

float clamp_unit(float value) {
  return clamp(value, 0.0f, 1.0f);
}

pair<wp::Vector2, wp::Vector2> calculateFovTriangle(wp::Vector2 const& pos, float viewAngle, float viewDist, float fov) {
  auto halfFov = fov * 0.5f;
  auto viewDistance = viewDist * 1.1f / cosf(WP_DEGTORAD(halfFov));

  return {
      pos + wp::Vector2::fromAngle(viewAngle - halfFov, wp::Clockwise) * viewDistance,
      pos + wp::Vector2::fromAngle(viewAngle + halfFov, wp::Clockwise) * viewDistance};
}

}  // namespace core
}  // namespace bw