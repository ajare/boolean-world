#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"

namespace bw::core {

// Generated coordinates and Voronoi vertices are canonicalized to this grid.
// Minimum-distance checks use exact generated coordinates; callers comparing
// rendered float values may allow one grid quantum of numeric tolerance.
inline constexpr float PrimitiveFieldNumericTolerance = 1.0f / 4096.0f;

struct BW_API PrimitiveFieldExtents {
  wp::Vector2 minimum;
  wp::Vector2 maximum;
};

struct BW_API PrimitiveFieldLayoutRequest {
  PrimitiveFieldExtents worldExtents;
  float minimumSpacing{128.0f};
  uint32_t maximumSites{2000};
  int32_t seed{0};
  int32_t lloydIterations{5};
};

struct BW_API PrimitiveFieldCell {
  // Counter-clockwise, without a repeated closing vertex, rotated to begin at
  // the lexicographically smallest vertex. The corresponding site has the same
  // index in PrimitiveFieldLayout::sites.
  std::vector<wp::Vector2> vertices;
};

struct BW_API PrimitiveFieldLayout {
  PrimitiveFieldExtents worldExtents;
  std::vector<wp::Vector2> sites;
  std::vector<PrimitiveFieldCell> cells;
};

struct BW_API PrimitiveFieldLayoutResult {
  std::optional<PrimitiveFieldLayout> layout;
  std::string error;

  [[nodiscard]] bool succeeded() const { return layout.has_value(); }
};

// Uses PCG-XSH-RR 32 with a fixed stream. Candidate offsets are derived with
// explicit integer arithmetic and converted to the public float grid by an
// exact power-of-two scale. Sampling starts at the world centre and expands
// through a distance-prioritized frontier. Each requested Lloyd pass proposes
// the bounded-cell centroid in stable site order, retaining the old site when
// the proposal would violate the inset domain or minimum spacing. Output is
// ordered by squared distance from the world centre, then x and y.
[[nodiscard]] BW_API PrimitiveFieldLayoutResult generatePrimitiveFieldLayout(
    PrimitiveFieldLayoutRequest const& request);

// Builds and validates the bounded Voronoi stage for caller-supplied sites.
// This is useful to consumers that already own a deterministic sampler and
// provides an explicit failure path for duplicate or degenerate site sets.
[[nodiscard]] BW_API PrimitiveFieldLayoutResult buildBoundedPrimitiveFieldLayout(
    PrimitiveFieldExtents const& worldExtents,
    std::vector<wp::Vector2> sites);

}  // namespace bw::core
