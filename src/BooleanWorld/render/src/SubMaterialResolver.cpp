#include <core/Defines.h>

#include "SubMaterialResolver.h"
#include "EmbossingCatalog.h"
#include "ProcMaterial.h"

using namespace std;

SubMaterialResolver::SubMaterialResolver(wp::application::resourcesystem::ResourceManager* resourceMgr) {
  for (auto const& resource : resourceMgr->getResourcesByType("ProcMaterial")) {
    auto procMaterial = static_cast<ProcMaterial*>(resource.get());

    for (auto const& subMaterial : procMaterial->getData().subMaterials) {
      Resolved resolved;
      resolved.materialIndex = subMaterial.materialIndex;
      resolved.def.baseColour = subMaterial.baseColour;
      // Embossing is composed per surface by resolve(); embedded legacy
      // Sub-material relief must never leak into a rendered definition.
      resolved.def.emboss = {};
      resolved.chipParameters = subMaterial.chip;
      resolved.def.params.fill(0.0f);

      for (size_t i = 0; i < subMaterial.paramValues.size() && i < resolved.def.params.size(); ++i) {
        resolved.def.params[i] = subMaterial.paramValues[i];
      }

      // Global uniqueness across every loaded catalog is already enforced
      // at resource-load time (ProcMaterialResourceDefinitionFactory), so
      // any collision here would mean a bug there, not here.
      mSubMaterials[subMaterial.id] = resolved;
    }
  }

  for (auto const& resource :
       resourceMgr->getResourcesByType("EmbossingCatalog")) {
    auto catalog = static_cast<EmbossingCatalog*>(resource.get());
    for (auto const& preset : catalog->getData().presets) {
      mEmbossPresets[preset.id] = preset.emboss;
    }
  }
}

SubMaterialResolver::Resolved SubMaterialResolver::resolve(
    string const& subMaterialId, string const& embossPresetId) const {
  Resolved resolved;
  auto material = mSubMaterials.find(subMaterialId);
  if (material != mSubMaterials.end()) {
    resolved = material->second;
  } else {
    resolved.materialIndex = BW_MATERIAL_ERROR_INDEX;
    resolved.def.params.fill(0.0f);
    resolved.def.baseColour = {1.0f, 0.0f, 1.0f};
    resolved.chipParameters = {};
  }

  auto emboss = mEmbossPresets.find(embossPresetId);
  resolved.def.emboss = emboss == mEmbossPresets.end()
                            ? bw::core::EmbossData{}
                            : emboss->second;
  return resolved;
}

bw::core::ChipParametersResolver SubMaterialResolver::chipParametersResolver() const {
  auto snapshot = mSubMaterials;
  return [snapshot = std::move(snapshot)](string const& subMaterialId) {
    auto const found = snapshot.find(subMaterialId);
    return found == snapshot.end() ? bw::core::ChipGenerationParameters{}
                                   : found->second.chipParameters;
  };
}
