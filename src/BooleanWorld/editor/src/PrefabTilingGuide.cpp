#include "PrefabTilingGuide.h"

#include <algorithm>
#include <array>

namespace editor {
namespace {

const std::array squareOutline{
    wp::Vector2{-0.5f, -0.5f},
    wp::Vector2{0.5f, -0.5f},
    wp::Vector2{0.5f, 0.5f},
    wp::Vector2{-0.5f, 0.5f},
};

const std::array tilingGuideDefinitions{
    PrefabTilingGuideDefinition{
        bw::core::PrefabTilingType::Square, "Square", squareOutline},
};

}  // namespace

std::span<PrefabTilingGuideDefinition const> prefabTilingGuideDefinitions() {
  return tilingGuideDefinitions;
}

std::string_view prefabTilingGuideName(bw::core::PrefabTilingType type) {
  auto const definitions = prefabTilingGuideDefinitions();
  auto const it = std::find_if(
      definitions.begin(), definitions.end(),
      [type](PrefabTilingGuideDefinition const& definition) {
        return definition.type == type;
      });
  return it != definitions.end() ? it->name : "Unknown";
}

std::vector<wp::Vector2> prefabTilingOutline(
    bw::core::PrefabTilingType type, float size) {
  auto const definitions = prefabTilingGuideDefinitions();
  auto const it = std::find_if(
      definitions.begin(), definitions.end(),
      [type](PrefabTilingGuideDefinition const& definition) {
        return definition.type == type;
      });
  if (it == definitions.end()) {
    return {};
  }

  std::vector<wp::Vector2> outline;
  outline.reserve(it->unitOutline.size());
  for (auto const& point : it->unitOutline) {
    outline.push_back(point * size);
  }
  return outline;
}

}  // namespace editor
