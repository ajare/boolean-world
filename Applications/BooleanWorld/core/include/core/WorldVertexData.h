#pragma once

#include <cstdint>
#include <array>

#include "Defines.h"
#include "PrimitivePropertySet.h"

namespace bw {
namespace core {
class Primitive;

struct WorldVertexData {
  /*
  enum struct InterpolationState
  {
          NotInterpolated,
          InterpolatedUnset,
          InterpolatedSet
  };

  InterpolationState state;
  */
  PrimitivePropertySet properties[2];
  uint32_t primitiveIndex{~0u};
};

}  // namespace core
}  // namespace bw
