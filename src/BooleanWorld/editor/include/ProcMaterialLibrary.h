#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <core/ProcMaterialData.h>

namespace wp::application::resourcesystem {
class ResourceManager;
}

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

// Returns Sub-materials ordered by display name, after case-insensitive
// regex-searching that name. An invalid expression returns no matches and
// reports a user-facing error through regexError.
std::vector<bw::core::SubMaterial const*> filterAndSortSubMaterials(
    std::vector<bw::core::SubMaterial> const& subMaterials,
    std::string_view expression, std::string* regexError);

// Read-only picker view of one loaded, file-authored Triplanar material.
// resourceName and albedoResourceName are qualified Resource names, which are
// both the displayed identity and the exact Surface material reference written
// by the picker.
struct TriplanarMaterialEntry {
  std::string resourceName;
  std::string albedoResourceName;
  float tileWidth{};
  float blendSharpness{};
};

// Discovers only resources that the editor's ResourceManager has successfully
// loaded. The EditorRenderSystem eagerly loads every declared Triplanar
// material, while this boundary keeps malformed/unavailable resources out of
// both picker paths.
[[nodiscard]] std::vector<TriplanarMaterialEntry>
discoverLoadedTriplanarMaterials(
    wp::application::resourcesystem::ResourceManager* resourceManager);

// Applies the same case-insensitive ECMAScript regex_search and alphabetical
// display-name ordering as filterAndSortSubMaterials.
[[nodiscard]] std::vector<TriplanarMaterialEntry const*>
filterAndSortTriplanarMaterials(
    std::vector<TriplanarMaterialEntry> const& materials,
    std::string_view expression, std::string* regexError);

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
  [[nodiscard]] uint64_t revision() const { return mRevision; }
  [[nodiscard]] ProcMaterialCatalog const* findCatalogForSubMaterial(
      std::string const& subMaterialId) const;
  [[nodiscard]] bw::core::SubMaterial const* findSubMaterial(
      std::string const& subMaterialId) const;
  // The first Sub-material across the loaded catalogs whose materialIndex
  // names this Technique, or nullptr when none does. Resolves defaults that
  // must name a Technique rather than a catalog position.
  [[nodiscard]] bw::core::SubMaterial const* findSubMaterialByMaterialIndex(
      uint32_t materialIndex) const;

  // Creates a globally unique stable id from displayName and returns it.
  // Parameters and colour are validated against the selected Technique schema;
  // Chip generation is validated against its own authoring limits and
  // relational constraints, which no Technique schema bounds.
  std::string createSubMaterial(
      std::string const& resourceName, std::string const& displayName,
      uint32_t materialIndex, std::vector<float> const& paramValues,
      std::array<float, 3> const& baseColour,
      bw::core::ChipGenerationParameters const& chip = {});
  void renameSubMaterial(std::string const& subMaterialId,
                         std::string const& displayName);
  void editSubMaterial(std::string const& subMaterialId,
                       std::vector<float> const& paramValues,
                       std::array<float, 3> const& baseColour,
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
