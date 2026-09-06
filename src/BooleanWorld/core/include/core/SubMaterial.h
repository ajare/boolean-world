#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/ChipGenerationParameters.h"
#include "core/Emboss.h"  // EmbossParameterLimits used by Chip bounds.
#include "core/Serializable.h"

namespace bw {
namespace core {

// A named, fully-parameterized instance of one Technique - see CONTEXT.md's
// "Sub-material" entry. `id` is the stable string a wall/floor/ceiling
// references; it must be unique within its own ProcMaterialData catalog
// (checked by ProcMaterialData::deserializeImpl) and, at a higher seam once
// multiple catalogs can be loaded together, across every loaded catalog.
struct SubMaterial : public Serializable {
  std::string id;
  std::string displayName;

  // Opaque stable reference into the app-side Acoustic catalog. Empty and
  // unknown ids are valid authored states; the app resolves either to its
  // documented fallback without involving core in acoustic calculations.
  std::string acousticPresetId;

  // Selects the Technique (and therefore the TechniqueSchema) this
  // Sub-material's paramValues are bounded by.
  uint32_t materialIndex{0};

  std::vector<float> paramValues;
  std::array<float, 3> baseColour{};

  // Eligibility, count, spacing, and size variation for Chips cut into this
  // Sub-material - see CONTEXT.md's "Chip" entry and ADR-0027. These are
  // independent of the Technique schema.
  ChipGenerationParameters chip;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

// Authoring bounds shared by the editor and deserialization. Relational
// constraints are checked by ChipParametersAreValid.
[[nodiscard]] EmbossParameterLimits ChipArrisLengthLimits();
[[nodiscard]] EmbossParameterLimits ChipDepthLimits();
[[nodiscard]] EmbossParameterLimits ChipReachLimits();
[[nodiscard]] EmbossParameterLimits ChipSpacingLimits();
[[nodiscard]] EmbossParameterLimits ChipProbabilityLimits();
[[nodiscard]] EmbossParameterLimits ChipCornerDistanceLimits();

// In addition to scalar bounds: each minimum may not exceed its maximum,
// maximum width (half maximum reach) may not exceed the minimum eligible Arris
// length, centre spacing is at least maximumReach + 0.1 so Arris Chips cannot
// overlap, and the allowed Chip type list is non-empty and duplicate-free.
[[nodiscard]] bool ChipParametersAreValid(
    ChipGenerationParameters const& parameters);

}  // namespace core
}  // namespace bw
