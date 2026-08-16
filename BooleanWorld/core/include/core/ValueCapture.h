#pragma once

#include "core/Platform.h"
#include "core/ValueCaptureMode.h"

namespace bw {
namespace core {
struct ValueCapture {
  ValueCaptureMode mode{ValueCaptureMode::DistanceSticky};
  float prevValue{std::numeric_limits<float>::quiet_NaN()};
  float curValue{0.0f};

public:
  ValueCapture() = default;

  ValueCapture(ValueCaptureMode _mode)
      : mode(_mode) {
  }
};

}  // namespace core
}  // namespace bw
