#pragma once

#include "core/Platform.h"

namespace bw {
namespace core {

enum struct WorldTimerValue {
  EndToEnd,
  TransformVertices,
  Clip,
  Triangulate
};

}  // namespace core
}  // namespace bw