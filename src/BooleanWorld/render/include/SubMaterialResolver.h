#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/MaterialDefinition.h>
#include <core/WorldDataGenerator.h>

// Resolves a Primitive surface's (Sub-material id, Emboss-preset id) pair.
// The Sub-material supplies Technique, parameters, colour, and Chips; the
// optional preset independently supplies only Embossing. Built from every
// ProcMaterial and the sole EmbossingCatalog in the ResourceManager. Unknown
// Sub-material ids use the error material, while empty or unknown preset ids
// safely resolve to no Embossing.
class SubMaterialResolver {
public:
  struct Resolved {
    int32_t materialIndex;
    bw::core::MaterialDefinitionData def;
    bw::core::ChipGenerationParameters chipParameters;
  };

private:
  std::unordered_map<std::string, Resolved> mSubMaterials;
  std::unordered_map<std::string, bw::core::EmbossData> mEmbossPresets;

public:
  explicit SubMaterialResolver(wp::application::resourcesystem::ResourceManager* resourceMgr);

  Resolved resolve(
      std::string const& subMaterialId,
      std::string const& embossPresetId = {}) const;

  // Returns a self-contained callback safe to retain on a generator after
  // this resolver is destroyed.
  bw::core::ChipParametersResolver chipParametersResolver() const;
};
