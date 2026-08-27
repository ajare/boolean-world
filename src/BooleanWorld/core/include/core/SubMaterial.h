#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/Emboss.h"
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

  // Selects the Technique (and therefore the TechniqueSchema) this
  // Sub-material's paramValues are bounded by.
  uint32_t materialIndex{0};

  std::vector<float> paramValues;
  std::array<float, 3> baseColour{};

  // The relief this Sub-material embosses into every surface it is applied
  // to. Unlike paramValues it is not bounded by the Technique schema: the
  // pattern is evaluated in world space by shared shader code, the same way
  // for every Technique, so its bounds live with the data - see
  // EmbossIsInRange.
  EmbossData emboss;

  // How far a Chip bites into this Sub-material, and how far it reaches along
  // the Arris it chips - see CONTEXT.md's "Chip" entry and ADR-0027. Neither
  // is bounded by the Technique schema, exactly like emboss above: the same
  // treatment, for the same reason. Zero means this Sub-material does not
  // chip at all, which is the default - nothing consumes these values yet,
  // so a fresh Sub-material must not appear to promise chipping it cannot
  // yet show.
  float chipDepth{0.0f};
  float chipReach{0.0f};

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

// Authoring bounds for chipDepth/chipReach, shared by the editor's sliders and
// deserialization's validation exactly as EmbossParameterLimits is - see
// EmbossParameterLimits.
[[nodiscard]] EmbossParameterLimits ChipDepthLimits();
[[nodiscard]] EmbossParameterLimits ChipReachLimits();

// Both fields within their own limits. Deserialization reports a violation
// rather than clamping, matching EmbossIsInRange.
[[nodiscard]] bool ChipIsInRange(float chipDepth, float chipReach);

}  // namespace core
}  // namespace bw
