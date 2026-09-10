#include "AcousticPresetResolver.h"

#include "ProcMaterial.h"

using namespace std;

AcousticPresetResolver::AcousticPresetResolver(
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  for (auto const& resource :
       resourceMgr->getResourcesByType("AcousticCatalog")) {
    auto catalog = static_cast<AcousticCatalog*>(resource.get());
    for (auto const& preset : catalog->getPresets()) {
      mPresets[preset.id] = preset;
    }
  }

  for (auto const& resource : resourceMgr->getResourcesByType("ProcMaterial")) {
    auto catalog = static_cast<ProcMaterial*>(resource.get());
    for (auto const& subMaterial : catalog->getData().subMaterials) {
      mSubMaterialPresetIds[subMaterial.id] = subMaterial.acousticPresetId;
    }
  }
}

AcousticPreset const& AcousticPresetResolver::resolve(
    string const& acousticPresetId) const {
  auto found = mPresets.find(acousticPresetId);
  return found == mPresets.end() ? defaultPreset() : found->second;
}

AcousticPreset const& AcousticPresetResolver::resolveSubMaterial(
    string const& subMaterialId) const {
  auto found = mSubMaterialPresetIds.find(subMaterialId);
  return found == mSubMaterialPresetIds.end()
             ? defaultPreset()
             : resolve(found->second);
}

AcousticPreset const& AcousticPresetResolver::resolveSurfaceMaterial(
    bw::core::SurfaceMaterialReference const& material) const {
  return material.kind == bw::core::SurfaceMaterialKind::Triplanar
             ? defaultPreset()
             : resolveSubMaterial(material.reference);
}

AcousticPreset const& AcousticPresetResolver::defaultPreset() {
  // Steam Audio's documented generic material. This code-owned copy makes
  // fallback deterministic even if no catalog was loaded; the built-in data
  // contains the same entry so an explicit reference resolves identically.
  static AcousticPreset const preset{
      DefaultPresetId,
      "Generic",
      {0.10f, 0.20f, 0.30f},
      0.05f,
      {0.100f, 0.050f, 0.030f}};
  return preset;
}
