#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <core/ProcMaterialData.h>

namespace editor {

struct ProcMaterialCatalog {
  std::string resourceName;
  std::filesystem::path filepath;
  bw::core::ProcMaterialData data;
};

// Editor-side, read-only discovery for the material picker. Resources.yaml is
// used only as a manifest; each ProcMaterial payload is parsed directly with
// bw::core::Serializer, never through ResourceManager (ADR-0024).
class ProcMaterialLibrary {
  std::vector<ProcMaterialCatalog> mCatalogs;

public:
  void load(std::filesystem::path const& resourcesManifest);

  [[nodiscard]] std::vector<ProcMaterialCatalog> const& catalogs() const;
  [[nodiscard]] ProcMaterialCatalog const* findCatalogForSubMaterial(
      std::string const& subMaterialId) const;
  [[nodiscard]] bw::core::SubMaterial const* findSubMaterial(
      std::string const& subMaterialId) const;
};

// The editor process's picker library. Tests may construct independent
// ProcMaterialLibrary instances without touching this singleton.
ProcMaterialLibrary& procMaterialLibrary();

}  // namespace editor
