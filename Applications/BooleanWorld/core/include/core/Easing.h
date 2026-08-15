#pragma once

#include "core/Platform.h"

namespace bw {
namespace core {

enum struct Easing {
  Linear,
  EaseInSine,
  EaseInCubic,
  EaseInQuintic,
  EaseOutSine,
  EaseOutCubic,
  EaseOutQuintic,
  EaseInOutSine,
  EaseInOutCubic,
  EaseInOutQuintic,
  EaseInBack,
  EaseOutBack,
  EaseInOutBack,
  EaseInExpo,
  EaseOutExpo,
  EaseInOutExpo,
  EaseInElastic,
  EaseOutElastic,
  EaseInOutElastic,
  EaseInBounce,
  EaseOutBounce,
  EaseInOutBounce
};

float ease(Easing easing, float v);

}  // namespace core
}  // namespace bw