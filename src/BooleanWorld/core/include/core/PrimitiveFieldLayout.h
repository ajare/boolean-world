#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
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
  // True only when sampling still had candidates but stopped at maximumSites.
  bool samplingStoppedAtMaximum{false};
};

struct BW_API PrimitiveFieldSiteCountEstimate {
  std::optional<uint64_t> uncappedSiteCount;
  std::string error;

  [[nodiscard]] bool succeeded() const {
    return uncappedSiteCount.has_value();
  }
};

// Cheap area/density estimate used by authoring UI. This validates extents and
// spacing but does not sample, relax, or construct Voronoi cells.
[[nodiscard]] BW_API PrimitiveFieldSiteCountEstimate
estimatePrimitiveFieldSiteCount(
    PrimitiveFieldExtents const& worldExtents,
    float minimumSpacing);

struct BW_API PrimitiveFieldLayoutResult {
  std::optional<PrimitiveFieldLayout> layout;
  std::string error;
  bool wasCancelled{false};

  [[nodiscard]] bool succeeded() const { return layout.has_value(); }
  [[nodiscard]] bool cancelled() const { return wasCancelled; }
};

enum class PrimitiveFieldLayoutPhase : uint8_t {
  Sampling,
  LloydRelaxation,
  VoronoiConstruction,
  Validation,
  Complete,
};

struct BW_API PrimitiveFieldLayoutProgress {
  PrimitiveFieldLayoutPhase phase{PrimitiveFieldLayoutPhase::Sampling};
  // Overall request completion in the inclusive range [0, 1].
  float completion{0.0f};
};

struct BW_API PrimitiveFieldLayoutExecution {
  std::stop_token stopToken;
  std::function<void(PrimitiveFieldLayoutProgress const&)> reportProgress;
};

// Uses PCG-XSH-RR 32 with a fixed stream. Candidate offsets are derived with
// explicit integer arithmetic and converted to the public float grid by an
// exact power-of-two scale. Sampling starts with a required site at the origin
// and expands through a distance-prioritized frontier. Lloyd relaxation pins
// the origin; each other site proposes the bounded-cell centroid in stable
// order and retains its old position when the proposal would violate the inset
// domain or minimum spacing. Output is ordered by squared distance from the
// world centre, then x and y.
[[nodiscard]] BW_API PrimitiveFieldLayoutResult generatePrimitiveFieldLayout(
    PrimitiveFieldLayoutRequest const& request);

// The execution controls are value/callback-only: the core never owns or
// reaches into caller state. Cancellation returns a result with cancelled()
// true, and progress is monotonic and ends with Complete/1 on success.
[[nodiscard]] BW_API PrimitiveFieldLayoutResult generatePrimitiveFieldLayout(
    PrimitiveFieldLayoutRequest const& request,
    PrimitiveFieldLayoutExecution const& execution);

// Builds and validates the bounded Voronoi stage for caller-supplied sites.
// This is useful to consumers that already own a deterministic sampler and
// provides an explicit failure path for duplicate or degenerate site sets.
[[nodiscard]] BW_API PrimitiveFieldLayoutResult buildBoundedPrimitiveFieldLayout(
    PrimitiveFieldExtents const& worldExtents,
    std::vector<wp::Vector2> sites);

}  // namespace bw::core
