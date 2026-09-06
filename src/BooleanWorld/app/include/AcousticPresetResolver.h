#pragma once

#include <string>
#include <unordered_map>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include "AcousticCatalog.h"

// Resolves the opaque id stored by core::SubMaterial. Empty and unknown ids
// deliberately use the Steam Audio "generic" material, so incomplete or
// stale authoring never prevents a World from loading or exporting.
class AcousticPresetResolver {
  std::unordered_map<std::string, AcousticPreset> mPresets;
  std::unordered_map<std::string, std::string> mSubMaterialPresetIds;

public:
  static constexpr char const* DefaultPresetId = "builtin.acoustic.generic";

  explicit AcousticPresetResolver(
      wp::application::resourcesystem::ResourceManager* resourceMgr);

  [[nodiscard]] AcousticPreset const& resolve(
      std::string const& acousticPresetId) const;

  // Convenience seam for generated surfaces, which carry a Sub-material id
  // rather than its referenced Acoustic preset id.
  [[nodiscard]] AcousticPreset const& resolveSubMaterial(
      std::string const& subMaterialId) const;

  [[nodiscard]] static AcousticPreset const& defaultPreset();
};
