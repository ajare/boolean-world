#include <exception>
#include <cmath>

#include <willpower/common/Globals.h>

#include "core/CoreException.h"
#include "core/Easing.h"

namespace bw {
namespace core {

using namespace std;

float easeOutBounce(float v) {
  const float n1 = 7.5625f;
  const float d1 = 2.75f;

  if (v < 1 / d1) {
    return n1 * v * v;
  } else if (v < 2 / d1) {
    return n1 * (v -= 1.5f / d1) * v + 0.75f;
  } else if (v < 2.5 / d1) {
    return n1 * (v -= 2.25f / d1) * v + 0.9375f;
  } else {
    return n1 * (v -= 2.625f / d1) * v + 0.984375f;
  }
}

float ease(Easing easing, float v) {
  float work;
  const float c1 = 1.70158f;
  const float c2 = c1 * 1.525f;
  const float c3 = 1 + c1;
  const float c4 = (2 * WP_PI) / 3;
  const float c5 = (2 * WP_PI) / 4.5f;

  switch (easing) {
    case Easing::Linear:
      return v;

    case Easing::EaseInSine:
      return 1 - cosf(v * WP_PI / 2);

    case Easing::EaseInCubic:
      return v * v * v;

    case Easing::EaseInQuintic:
      return v * v * v * v * v;

    case Easing::EaseOutSine:
      return sinf(v * WP_PI / 2);

    case Easing::EaseOutCubic:
      work = 1.0f - v;
      return 1.0f - work * work * work;

    case Easing::EaseOutQuintic:
      work = 1.0f - v;
      return 1.0f - work * work * work * work * work;

    case Easing::EaseInOutSine:
      return -(cosf(WP_PI * v) - 1) / 2;

    case Easing::EaseInOutCubic:
      return v < 0.5 ? 4 * v * v * v : 1 - powf(-2 * v + 2, 3) / 2;

    case Easing::EaseInOutQuintic:
      return v < 0.5 ? 16 * v * v * v * v * v : 1 - powf(-2 * v + 2, 5) / 2;

    case Easing::EaseInBack:
      return c3 * v * v * v - c1 * v * v;

    case Easing::EaseOutBack:
      return 1 + c3 * powf(v - 1, 3) + c1 * powf(v - 1, 2);

    case Easing::EaseInOutBack:
      return v < 0.5f
                 ? (powf(2 * v, 2) * ((c2 + 1) * 2 * v - c2)) / 2
                 : (powf(2 * v - 2, 2) * ((c2 + 1) * (v * 2 - 2) + c2) + 2) / 2;

    case Easing::EaseInExpo:
      return v == 0 ? 0 : powf(2, 10 * v - 10);

    case Easing::EaseOutExpo:
      return v == 1 ? 1 : 1 - powf(2, -10 * v);

    case Easing::EaseInOutExpo:
      return v == 0 ? 0 : v == 1 ? 1
                      : v < 0.5  ? powf(2, 20 * v - 10) / 2
                                 : (2 - powf(2, -20 * v + 10)) / 2;

    case Easing::EaseInElastic:
      return v == 0   ? 0
             : v == 1 ? 1
                      : -powf(2, 10 * v - 10) * sinf((v * 10 - 10.75f) * c4);

    case Easing::EaseOutElastic:
      return v == 0 ? 0 : v == 1 ? 1
                                 : powf(2, -10 * v) * sinf((v * 10 - 0.75f) * c4) + 1;

    case Easing::EaseInOutElastic:
      return v == 0 ? 0 : v == 1 ? 1
                      : v < 0.5f ? -(powf(2, 20 * v - 10) * sinf((20 * v - 11.125f) * c5)) / 2
                                 : (powf(2, -20 * v + 10) * sinf((20 * v - 11.125f) * c5)) / 2 + 1;

    case Easing::EaseInBounce:
      return 1.0f - easeOutBounce(1 - v);

    case Easing::EaseOutBounce:
      return easeOutBounce(v);

    case Easing::EaseInOutBounce:
      return v < 0.5f
                 ? (1 - easeOutBounce(1 - 2 * v)) / 2
                 : (1 + easeOutBounce(2 * v - 1)) / 2;

    default:
      throw CoreException("Unknown easing type.");
  }
}

}  // namespace core
}  // namespace bw