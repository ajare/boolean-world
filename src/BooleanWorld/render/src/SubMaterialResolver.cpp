#include <core/Defines.h>

#include "SubMaterialResolver.h"
#include "ProcMaterial.h"

using namespace std;

SubMaterialResolver::SubMaterialResolver(wp::application::resourcesystem::ResourceManager* resourceMgr) {
  for (auto const& resource : resourceMgr->getResourcesByType("ProcMaterial")) {
    auto procMaterial = static_cast<ProcMaterial*>(resource.get());

    for (auto const& subMaterial : procMaterial->getData().subMaterials) {
      Resolved resolved;
      resolved.materialIndex = subMaterial.materialIndex;
      resolved.def.baseColour = subMaterial.baseColour;
      resolved.def.emboss = subMaterial.emboss;
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
}

SubMaterialResolver::Resolved SubMaterialResolver::resolve(string const& subMaterialId) const {
  auto it = mSubMaterials.find(subMaterialId);
  if (it != mSubMaterials.end()) {
    return it->second;
  }

  Resolved error;
  error.materialIndex = BW_MATERIAL_ERROR_INDEX;
  error.def.emboss = {};
  error.def.params.fill(0.0f);
  error.def.baseColour = {1.0f, 0.0f, 1.0f};
  return error;
}
