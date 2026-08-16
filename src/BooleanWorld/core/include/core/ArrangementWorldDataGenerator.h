#pragma once

#include <cstdint>
#include <vector>

#include "core/Arrangement.h"
#include "core/LayerSelection.h"
#include "core/Platform.h"

namespace bw::core {
class Primitive;
class World;

// Converts authored floating-point polygons straight onto the topology grid.
// Contour roles are deliberately not carried; the arrangement derives them.
[[nodiscard]] BW_API std::vector<arr::Contour> ConvertPrimitiveToContours(
    Primitive const& primitive);

class BW_API ArrangementWorldDataGenerator {
  LayerSelection mLayerSelection{SelectLayer(0)};
  arr::ArrangementResultPtr mWorldData;

public:
  ArrangementWorldDataGenerator();

  void setLayerSelection(LayerSelection const& selection);

  [[nodiscard]] LayerSelection const& getLayerSelection() const;

  void setActiveLayer(uint8_t layer);

  void generate(World const* world);

  // Comparison and migration consumers can provide the exact generation-local
  // primitive ordering used by the legacy generator.
  void generate(std::vector<Primitive*> const& primitives);

  [[nodiscard]] arr::ArrangementResultPtr getWorldData() const;
};
}  // namespace bw::core
