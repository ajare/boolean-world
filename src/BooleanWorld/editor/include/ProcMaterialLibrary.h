#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <core/Emboss.h>
#include <core/ProcMaterialData.h>

namespace editor {

struct ProcMaterialCatalog {
  std::string resourceName;
  std::filesystem::path filepath;
  bw::core::ProcMaterialData data;
};

struct ProcMaterialLibrarySnapshot {
  std::vector<ProcMaterialCatalog> catalogs;
  uint64_t revision{0};
};

// Editor-side discovery and authoring for the material picker. Resources.yaml
// is used only as a manifest; each ProcMaterial payload is read and written
// directly with bw::core::Serializer, never through ResourceManager
// (ADR-0024).
class ProcMaterialLibrary {
  std::vector<ProcMaterialCatalog> mCatalogs;
  uint64_t mRevision{0};

  ProcMaterialCatalog& findCatalog(std::string const& resourceName);
  void save(ProcMaterialCatalog const& catalog) const;

public:
  void load(std::filesystem::path const& resourcesManifest);

  [[nodiscard]] std::vector<ProcMaterialCatalog> const& catalogs() const;
  [[nodiscard]] ProcMaterialCatalog const* findCatalogForSubMaterial(
      std::string const& subMaterialId) const;
  [[nodiscard]] bw::core::SubMaterial const* findSubMaterial(
      std::string const& subMaterialId) const;

  // Creates a globally unique stable id from displayName and returns it.
  // Parameters and colour are validated against the selected Technique schema;
  // emboss and Chip generation are validated against their own authoring
  // limits and relational constraints, which no Technique schema bounds.
  std::string createSubMaterial(
      std::string const& resourceName, std::string const& displayName,
      uint32_t materialIndex, std::vector<float> const& paramValues,
      std::array<float, 3> const& baseColour,
      bw::core::EmbossData const& emboss = {},
      bw::core::ChipGenerationParameters const& chip = {});
  void renameSubMaterial(std::string const& subMaterialId,
                         std::string const& displayName);
  void editSubMaterial(std::string const& subMaterialId,
                       std::vector<float> const& paramValues,
                       std::array<float, 3> const& baseColour,
                       bw::core::EmbossData const& emboss = {},
                       bw::core::ChipGenerationParameters const& chip = {});
  void deleteSubMaterial(std::string const& subMaterialId);

  [[nodiscard]] ProcMaterialLibrarySnapshot captureSnapshot() const;
  // Undo/redo restoration also persists the restored catalogs to their owning
  // YAML files, so disk and the live preview always describe the same data.
  void restoreSnapshot(ProcMaterialLibrarySnapshot const& snapshot);
};

// The editor process's picker library. Tests may construct independent
// ProcMaterialLibrary instances without touching this singleton.
ProcMaterialLibrary& procMaterialLibrary();

}  // namespace editor
