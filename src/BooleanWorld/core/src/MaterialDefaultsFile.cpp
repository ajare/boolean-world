#include <array>
#include <optional>
#include <vector>

#include <yaml-cpp/yaml.h>

// MaterialRegistry sizes its tables with BW_MATERIAL_COUNT and
// BW_MATERIAL_PARAMS_MAX, so core's defines have to land first.
#include "core/Defines.h"

#include <common/GameDefines.h>
#include <common/MaterialRegistry.h>

#include "core/MaterialDefaultsFile.h"

namespace bw {
namespace core {
namespace {

struct ParamOverride {
  float minimum{};
  float maximum{};
  float defaultValue{};
};

using MaterialOverrideRow =
    std::array<std::optional<ParamOverride>, BW_MATERIAL_PARAMS_MAX>;

std::array<MaterialOverrideRow, BW_MATERIAL_COUNT> gOverrides;

std::optional<uint32_t> findMaterialIndex(std::string const& name) {
  for (size_t i = 0; i < bw::common::MaterialNames.size(); ++i) {
    if (std::get<0>(bw::common::MaterialNames[i]) == name) {
      return (uint32_t)i;
    }
  }
  return std::nullopt;
}

std::optional<uint32_t> findParamIndex(uint32_t materialIndex, std::string const& name) {
  auto const& table = bw::common::MaterialParams[materialIndex];
  auto declared = std::get<1>(bw::common::MaterialNames[materialIndex]);
  for (uint32_t i = 0; i < declared && i < table.size(); ++i) {
    if (std::get<0>(table[i]) == name) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

void loadMaterialDefaultsFile(std::string const& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (YAML::Exception const&) {
    // No file, or not YAML at all: every material keeps its compiled-in
    // values. This is the expected outcome when nothing has been retuned.
    return;
  }

  auto materials = root["Configuration"]["Materials"];
  if (!materials || !materials.IsSequence()) {
    return;
  }

  for (auto const& materialNode : materials) {
    if (!materialNode["name"]) {
      continue;
    }
    auto materialIndex = findMaterialIndex(materialNode["name"].as<std::string>());
    if (!materialIndex) {
      // Named a material this build does not know - ignored rather than
      // failing, so an older/newer Game.yaml stays usable across a rebuild
      // that added or removed a material.
      continue;
    }

    auto params = materialNode["params"];
    if (!params || !params.IsSequence()) {
      continue;
    }
    for (auto const& paramNode : params) {
      if (!paramNode["name"]) {
        continue;
      }
      auto paramIndex =
          findParamIndex(*materialIndex, paramNode["name"].as<std::string>());
      if (!paramIndex) {
        continue;
      }

      ParamOverride override;
      auto const& compiled =
          bw::common::MaterialParams[*materialIndex][*paramIndex];
      override.minimum = paramNode["min"]
                              ? paramNode["min"].as<float>()
                              : std::get<1>(compiled);
      override.maximum = paramNode["max"]
                              ? paramNode["max"].as<float>()
                              : std::get<2>(compiled);
      override.defaultValue = paramNode["default"]
                                   ? paramNode["default"].as<float>()
                                   : std::get<3>(compiled);
      gOverrides[*materialIndex][*paramIndex] = override;
    }
  }
}

void clearMaterialDefaultOverrides() {
  gOverrides = {};
}

float materialParamMinimum(uint32_t materialIndex, uint32_t paramIndex) {
  if (materialIndex < gOverrides.size() &&
      paramIndex < gOverrides[materialIndex].size() &&
      gOverrides[materialIndex][paramIndex]) {
    return gOverrides[materialIndex][paramIndex]->minimum;
  }
  if (materialIndex >= bw::common::MaterialParams.size() ||
      paramIndex >= bw::common::MaterialParams[materialIndex].size()) {
    return 0.0f;
  }
  return std::get<1>(bw::common::MaterialParams[materialIndex][paramIndex]);
}

float materialParamMaximum(uint32_t materialIndex, uint32_t paramIndex) {
  if (materialIndex < gOverrides.size() &&
      paramIndex < gOverrides[materialIndex].size() &&
      gOverrides[materialIndex][paramIndex]) {
    return gOverrides[materialIndex][paramIndex]->maximum;
  }
  if (materialIndex >= bw::common::MaterialParams.size() ||
      paramIndex >= bw::common::MaterialParams[materialIndex].size()) {
    return 0.0f;
  }
  return std::get<2>(bw::common::MaterialParams[materialIndex][paramIndex]);
}

float materialParamDefault(uint32_t materialIndex, uint32_t paramIndex) {
  if (materialIndex < gOverrides.size() &&
      paramIndex < gOverrides[materialIndex].size() &&
      gOverrides[materialIndex][paramIndex]) {
    return gOverrides[materialIndex][paramIndex]->defaultValue;
  }
  if (materialIndex >= bw::common::MaterialParams.size() ||
      paramIndex >= bw::common::MaterialParams[materialIndex].size()) {
    return 0.0f;
  }
  return std::get<3>(bw::common::MaterialParams[materialIndex][paramIndex]);
}

}  // namespace core
}  // namespace bw
