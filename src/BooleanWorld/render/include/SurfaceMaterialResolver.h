#pragma once

#include <string>

#include <core/SurfaceMaterialReference.h>

#include "SubMaterialResolver.h"
#include "TriplanarMaterial.h"

// Resolves the explicit Surface-material tag without registry inference. A
// Triplanar reference must name a loaded TriplanarMaterial; Sub-materials keep
// the existing debug fallback behavior.
class SurfaceMaterialResolver {
public:
  struct Resolved {
    int32_t materialIndex{};
    bw::core::MaterialDefinitionData def{};
    TriplanarMaterial const* triplanar{};
    uint64_t bucketHash{};

    [[nodiscard]] bool isTriplanar() const { return triplanar != nullptr; }
    [[nodiscard]] uint64_t hash() const { return bucketHash; }
  };

private:
  SubMaterialResolver const* mSubMaterials{};
  wp::application::resourcesystem::ResourceManager* mResourceManager{};
  std::string mCurrentNamespace;

public:
  SurfaceMaterialResolver(
      SubMaterialResolver const& subMaterials,
      wp::application::resourcesystem::ResourceManager* resourceManager,
      std::string currentNamespace);

  [[nodiscard]] Resolved resolve(
      bw::core::SurfaceMaterialReference const& reference,
      std::string const& embossPresetId = {}) const;
};
