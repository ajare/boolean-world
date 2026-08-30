#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace bw {
namespace core {

class Serializer;

// The tiling pattern an Emboss preset lays over an assigned surface. The
// values are the ones world_pbr.frag/world_pbr_2d.frag switch on for
// EMBOSS_PATTERN, so they are part of the shader contract and must not be
// renumbered without changing both shaders.
enum class EmbossPattern : int32_t {
  None = 0,
  Square = 1,
  Hexagon = 2,
  RunningBond = 3,
  ModularOpus = 4,
  Voronoi = 5,
};

inline constexpr int32_t EmbossPatternCount = 6;

// The stable name written to and read from YAML, and shown in the editor's
// pattern picker. Unknown input reads back as None rather than throwing, so a
// catalog authored against a newer build still loads.
[[nodiscard]] char const* EmbossPatternName(EmbossPattern pattern);
[[nodiscard]] EmbossPattern EmbossPatternFromName(std::string const& name);

// Whether a pattern uses each of the shape parameters below. The editor hides
// the ones that do nothing for the selected pattern, exactly as the shader
// ignores them.
[[nodiscard]] bool EmbossPatternUsesRunningBond(EmbossPattern pattern);
[[nodiscard]] bool EmbossPatternUsesVoronoiRounding(EmbossPattern pattern);

// How the tile size reads for each pattern - "Hexagon radius", "Tile length",
// and so on. One source of truth for the editor's slider label.
[[nodiscard]] char const* EmbossRadiusName(EmbossPattern pattern);

// The relief an Emboss preset applies to its assigned surface: a tiling
// pattern turned into a normal-map perturbation by the fragment shader, with
// no effect on geometry or collision. It is resolved into the material
// definition, so surfaces with different presets get their own mesh buckets
// - see MaterialDefinitionData::hash.
//
// Defaults represent the canonical no-relief preset value.
struct EmbossData {
  EmbossPattern pattern{EmbossPattern::None};
  // Tile size in world units - a hexagon's radius, a square's half-size, a
  // running-bond tile's length, an opus lattice's large tile, a Voronoi cell.
  float radius{16.0f};
  // Depth of the groove between tiles, in world units of normal-map relief.
  float depth{0.5f};
  // How much each tile's own height varies from its neighbours', 0 to 1.
  float depthVariation{0.1f};
  // Running bond only: tile width and per-row offset, as percentages of the
  // tile length.
  float runningBondWidth{50.0f};
  float runningBondOffset{50.0f};
  // Voronoi only: how far the cell edges are rounded off, 0 to 1.
  float voronoiRounding{0.25f};

  bool operator==(EmbossData const& other) const = default;
};

// The authoring bounds for each field, shared by the editor's sliders and by
// deserialization's validation so neither can drift from the other.
struct EmbossParameterLimits {
  float minimum;
  float maximum;
};

[[nodiscard]] EmbossParameterLimits EmbossRadiusLimits();
[[nodiscard]] EmbossParameterLimits EmbossDepthLimits();
[[nodiscard]] EmbossParameterLimits EmbossDepthVariationLimits();
[[nodiscard]] EmbossParameterLimits EmbossRunningBondWidthLimits();
[[nodiscard]] EmbossParameterLimits EmbossRunningBondOffsetLimits();
[[nodiscard]] EmbossParameterLimits EmbossVoronoiRoundingLimits();

// Every field within its own limits. Deserialization reports a violation
// rather than clamping.
[[nodiscard]] bool EmbossIsInRange(EmbossData const& emboss);

// Writes the block under `name`. Always written, so a saved Emboss preset
// states its complete relief explicitly rather than relying on defaults.
void SerializeEmboss(
    std::shared_ptr<Serializer> const& serializer, std::string const& name,
    EmbossData const& emboss);

// Reads the block under `name`, field by field, each optional. An omitted
// block reads back as the canonical no-relief value.
[[nodiscard]] EmbossData DeserializeEmboss(
    std::shared_ptr<Serializer> const& serializer, std::string const& name);

}  // namespace core
}  // namespace bw
