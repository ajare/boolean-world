#pragma once

#include <string>
#include <vector>

#include "core/Serializable.h"
#include "core/SubMaterial.h"
#include "core/TechniqueSchema.h"

namespace bw {
namespace core {

// The core data model for a ProcMaterial catalog - see CONTEXT.md's
// "ProcMaterial" entry. This is data only: no Resource/ResourceManager
// wrapper, no editor UI, no renderer consumption. A future ProcMaterial
// Resource (issue #257) wraps exactly this type, the same way a TextFile-
// backed resource wraps parsed content elsewhere in this codebase.
//
// Deserialization validates, in order: each TechniqueSchema and SubMaterial
// individually (structural bounds - see their own deserializeImpl), then
// cross-references between them - every SubMaterial::materialIndex must
// name a TechniqueSchema present in this catalog, a SubMaterial's
// paramValues count must match that schema's parameter count, each value
// must fall within its schema parameter's [minimum, maximum], and every
// SubMaterial::id must be unique within this catalog. Any violation is
// reported via getDeserializationErrors() (Serializable's existing
// collected-error mechanism), not a crash or a silent clamp.
struct ProcMaterialData : public Serializable {
  std::string program3d;
  std::string program2d;

  std::vector<TechniqueSchema> techniqueSchemas;
  std::vector<SubMaterial> subMaterials;

  // Exposed so a future cross-catalog global-uniqueness check (once
  // multiple catalogs can be loaded together) can enumerate this catalog's
  // ids without needing a data-shape change.
  std::vector<std::string> subMaterialIds() const;

  TechniqueSchema const* findTechniqueSchema(uint32_t materialIndex) const;

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
