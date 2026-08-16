#pragma once

#include "Platform.h"

namespace bw {
namespace core {

enum struct ValueCaptureMode {
  DistanceSticky,
  DistanceDeltaUp,
  DistanceDeltaDown,
  DistanceLatchedUp,
  DistanceLatchedDown,
  AngleSticky,
  AngleDeltaUp,
  AngleDeltaDown,
  AngleLatchedUp,
  AngleLatchedDown,
  COUNT
};

}  // namespace core
}  // namespace bw
