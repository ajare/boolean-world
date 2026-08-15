#pragma once

#include <array>
#include <vector>

#include "core/Platform.h"

namespace bw {
namespace core {
enum struct AnimatedPropertyEventTriggerType {
  Up,
  Down,
  UpDown
};

struct AnimatedPropertyEvent {
  uint32_t eventType;
  AnimatedPropertyEventTriggerType triggerType;
  float value;
};

}  // namespace core
}  // namespace bw
