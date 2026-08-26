#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/MaterialDefinition.h>

// Resolves a Primitive surface's Sub-material id (PrimitivePropertySet::
// floorMaterialId/ceilingMaterialId/wallMaterialId) to the Technique index,
// parameters and base colour WorldRenderer/WorldRenderer3d bind as
// MATERIAL_INDEX/MATERIAL_PARAMS uniforms - see issue #261 and ADR-0023.
// Built once from every "ProcMaterial" resource loaded in the given
// ResourceManager; an id that names no Sub-material in any of them
// (missing/unknown/empty) resolves to BW_MATERIAL_ERROR_INDEX instead of
// undefined behaviour.
class SubMaterialResolver {
public:
  struct Resolved {
    uint32_t materialIndex;
    bw::core::MaterialDefinitionData def;
  };

private:
  std::unordered_map<std::string, Resolved> mSubMaterials;

public:
  explicit SubMaterialResolver(wp::application::resourcesystem::ResourceManager* resourceMgr);

  Resolved resolve(std::string const& subMaterialId) const;
};
