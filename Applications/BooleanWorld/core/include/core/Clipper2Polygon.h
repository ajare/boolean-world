#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include "core/Edge.h"

namespace bw {
namespace core {

struct Clipper2Polygon {
  bool isHole;
  uint32_t primitiveIndex;
  Clipper2Lib::Path64 path;
};

}  // namespace core
}  // namespace bw