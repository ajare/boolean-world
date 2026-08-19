#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "core/Defines.h"

namespace bw::core {
// The set of Layers a generation folds across, indexed by stable Layer id
// (docs/adr/0013). Layer ids beyond the mask's width cannot be selected.
using LayerSelection = std::bitset<256>;

[[nodiscard]] inline bool IsLayerSelectable(uint32_t layerId) {
  return size_t(layerId) < LayerSelection{}.size();
}

[[nodiscard]] inline bool IsLayerSelected(
    LayerSelection const& selection, uint32_t layerId) {
  return IsLayerSelectable(layerId) && selection.test(size_t(layerId));
}

inline LayerSelection SelectLayer(uint32_t layerId) {
  LayerSelection selection;
  if (IsLayerSelectable(layerId)) {
    selection.set(size_t(layerId));
  }
  return selection;
}

inline LayerSelection SelectAllLayers() {
  LayerSelection selection;
  selection.set();
  return selection;
}
}  // namespace bw::core
