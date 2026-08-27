#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace bw::core {

enum class ChipType {
  Tapered,
  PrismaticNotch,
  PyramidalDivot,
  MultiFacetSpall,
  SteppedFracture,
  VShapedNotch,
  TrapezoidalSpall
};

inline constexpr std::array AllChipTypes{
    ChipType::Tapered,
    ChipType::PrismaticNotch,
    ChipType::PyramidalDivot,
    ChipType::MultiFacetSpall,
    ChipType::SteppedFracture,
    ChipType::VShapedNotch,
    ChipType::TrapezoidalSpall};

[[nodiscard]] std::string_view ChipTypeName(ChipType type);
[[nodiscard]] std::optional<ChipType> ChipTypeFromName(std::string_view name);

// Per-Sub-material controls for deterministic procedural Chips. Reach is an
// Arris Chip's total length; width is half that reach. `types` is the
// non-empty set of profiles this material may choose from. Corner distances
// are measured independently from the trihedral vertex along its three edges.
struct ChipGenerationParameters {
  float minimumArrisLength{2.0f};
  float minimumDepth{1.0f};
  float maximumDepth{3.0f};
  float minimumReach{1.0f};
  float maximumReach{3.0f};
  float minimumSpacing{3.1f};
  float probability{0.0f};
  float minimumCornerDistance{1.0f};
  float maximumCornerDistance{3.0f};
  float cornerProbability{0.0f};
  std::vector<ChipType> types{ChipType::Tapered};

  bool operator==(ChipGenerationParameters const&) const = default;
};

}  // namespace bw::core
