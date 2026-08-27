#pragma once

namespace bw::core {

// Per-Sub-material controls for deterministic procedural Chips. Reach is the
// Chip's total length along an Arris; width is half that reach.
struct ChipGenerationParameters {
  float minimumArrisLength{2.0f};
  float minimumDepth{1.0f};
  float maximumDepth{3.0f};
  float minimumReach{1.0f};
  float maximumReach{3.0f};
  float minimumSpacing{3.1f};
  float probability{0.0f};

  bool operator==(ChipGenerationParameters const&) const = default;
};

}  // namespace bw::core
