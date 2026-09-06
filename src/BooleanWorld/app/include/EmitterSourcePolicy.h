#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bw::app {

struct EmitterSourcePlacement {
  int32_t tileX{};
  int32_t tileY{};
  uint32_t gridSize{};

  bool operator==(EmitterSourcePlacement const&) const = default;
};

struct EmitterSourceIdentity {
  std::string guid;
  std::optional<EmitterSourcePlacement> placement;

  bool operator==(EmitterSourceIdentity const&) const = default;
};

struct EmitterSourceReconciliation {
  std::vector<std::pair<std::size_t, std::size_t>> retained;
  std::vector<std::size_t> removed;
  std::vector<std::size_t> added;
};

// One-to-one reconciliation by (GUID, placement key). retained pairs index
// previous and next respectively and are the records whose playback timeline
// must be preserved.
[[nodiscard]] EmitterSourceReconciliation reconcileEmitterSources(
    std::vector<EmitterSourceIdentity> const& previous,
    std::vector<EmitterSourceIdentity> const& next);

// Pure inputs to the emitter broad-phase and reflection budget. Distances and
// radii use the same world units. key is a stable caller-owned identity used
// only to make equal-distance ordering deterministic.
struct EmitterSourceCandidate {
  std::size_t key{};
  float distance{};
  float cullRadius{};
  bool previouslySelectedForReflections{false};
};

struct EmitterSourceDecision {
  std::size_t key{};
  float normalizedDistance{};
  bool inRange{false};
  bool selectedForReflections{false};
};

[[nodiscard]] bool emitterSourceInRange(float distance, float cullRadius);

[[nodiscard]] float normalizedEmitterSourceDistance(
    float distance, float cullRadius);

// Selects at most reflectionCapacity in-range sources. Existing selections
// retain their slots until a challenger is nearer by more than
// hysteresisMargin in normalized-distance space.
[[nodiscard]] std::vector<EmitterSourceDecision> selectEmitterSources(
    std::vector<EmitterSourceCandidate> const& candidates,
    std::size_t reflectionCapacity,
    float hysteresisMargin);

// Advances a [0, 1] contribution toward its target over fadeDuration.
[[nodiscard]] float advanceEmitterFade(
    float contribution, bool enabled, float frameTime, float fadeDuration);

}  // namespace bw::app
