#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "core/Defines.h"

namespace bw::core {
using LayerSelection = std::bitset<256>;

inline LayerSelection SelectLayer(uint8_t layer) {
  LayerSelection selection;
  if (layer == BW_LAYER_ALL) {
    selection.set();
  } else {
    selection.set(size_t(layer));
  }
  return selection;
}
}  // namespace bw::core
