#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <core/EmbossingCatalogData.h>

namespace editor {

struct EmbossingCatalogSnapshot {
  std::filesystem::path filepath;
  bw::core::EmbossingCatalogData data;
  uint64_t revision{0};
};

// Editor-side direct-file authoring for the sole global Embossing catalog.
// Resources.yaml is only the discovery manifest; the payload is read and
// written through the shared core serializer.
class EmbossingCatalogLibrary {
  std::filesystem::path mFilepath;
  std::string mResourceName;
  bw::core::EmbossingCatalogData mData;
  uint64_t mRevision{0};

  void save() const;

public:
  void load(std::filesystem::path const& resourcesManifest);

  [[nodiscard]] bw::core::EmbossingCatalogData const& data() const;
  [[nodiscard]] std::string const& resourceName() const;
  [[nodiscard]] bw::core::EmbossPreset const* findPreset(
      std::string const& presetId) const;

  // Creates and returns a catalog-unique stable id derived from displayName.
  std::string createPreset(std::string const& displayName,
                           bw::core::EmbossData const& emboss);
  // Rename changes display text only; the id held by Primitive surfaces is
  // deliberately stable.
  void renamePreset(std::string const& presetId,
                    std::string const& displayName);
  void editPreset(std::string const& presetId,
                  bw::core::EmbossData const& emboss);
  void deletePreset(std::string const& presetId);

  [[nodiscard]] EmbossingCatalogSnapshot captureSnapshot() const;
  // Restoring undo/redo state also persists it, keeping disk and memory in
  // lockstep.
  void restoreSnapshot(EmbossingCatalogSnapshot const& snapshot);
};

EmbossingCatalogLibrary& embossingCatalogLibrary();

}  // namespace editor
