#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/Arrangement.h"
#include "core/LayerSelection.h"
#include "core/Platform.h"

namespace bw::core {
class Primitive;
class World;

// Result of converting one Primitive's authored floating-point polygons
// straight onto the topology grid. Contour roles are deliberately not
// carried; the arrangement derives them.
struct PrimitiveContours {
  std::vector<arr::Contour> contours;
  // Per-contour, per-edge wall collision/visibility overrides, parallel to
  // contours: edgeOverrides[c][i]/edgeVisibleOverrides[c][i] is the override
  // for the edge from contours[c][i] to contours[c][(i+1)%contours[c].size()].
  // Populated only for MeshPrimitives, sourced from External edges' authored
  // collides/visible flags (see #244/#246, ADR-0022); every other Primitive
  // kind, and every non-External edge of a MeshPrimitive, leaves
  // std::nullopt in place.
  std::vector<std::vector<std::optional<bool>>> edgeOverrides;
  std::vector<std::vector<std::optional<bool>>> edgeVisibleOverrides;
};

[[nodiscard]] BW_API PrimitiveContours ConvertPrimitiveToContours(
    Primitive const& primitive);

// Copies every generation input needed by the arrangement so worker execution
// never has to reach back into live authored primitives.
[[nodiscard]] BW_API std::vector<arr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives,
    std::vector<uint64_t> const& generatedPriorities = {});

class BW_API ArrangementWorldDataGenerator {
  arr::ArrangementResultPtr mWorldData;

public:
  ArrangementWorldDataGenerator();

  void generate(
      World const* world,
      LayerSelection const& selection = SelectLayer(0));

  // Comparison and migration consumers can provide authored Primitives;
  // this overload retains their traditional authored-priority ordering.
  void generate(std::vector<Primitive*> const& primitives);

  // Generates an already ordered step-aware list with its effective
  // generation priorities.
  void generateOrdered(
      std::vector<Primitive*> const& primitives,
      std::vector<uint64_t> const& generatedPriorities);

  [[nodiscard]] arr::ArrangementResultPtr getWorldData() const;
};
}  // namespace bw::core
