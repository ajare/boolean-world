#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "core/Defines.h"

namespace bw::core {
// The set of Layers a generation folds across, indexed by stable Layer id
// (docs/adr/0013).
using LayerSelection = std::bitset<256>;

inline LayerSelection SelectLayer(uint8_t layer) {
  LayerSelection selection;
  selection.set(size_t(layer));
  return selection;
}

inline LayerSelection SelectAllLayers() {
  LayerSelection selection;
  selection.set();
  return selection;
}
}  // namespace bw::core
