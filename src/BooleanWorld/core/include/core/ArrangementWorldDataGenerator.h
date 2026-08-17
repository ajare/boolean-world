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

// Copies every generation input needed by the arrangement so worker execution
// never has to reach back into live authored primitives.
[[nodiscard]] BW_API std::vector<arr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives);

class BW_API ArrangementWorldDataGenerator {
  arr::ArrangementResultPtr mWorldData;

public:
  ArrangementWorldDataGenerator();

  void generate(
      World const* world,
      LayerSelection const& selection = SelectLayer(0));

  // Comparison and migration consumers can provide the exact generation-local
  // primitive ordering used by the legacy generator.
  void generate(std::vector<Primitive*> const& primitives);

  [[nodiscard]] arr::ArrangementResultPtr getWorldData() const;
};
}  // namespace bw::core
