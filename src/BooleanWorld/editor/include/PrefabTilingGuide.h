#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <willpower/common/Vector2.h>

#include <core/DefinePrefabs.h>

namespace editor {

// The data that describes each available Prefab tiling guide. A new tiling
// type adds one definition; renderers consume the resulting outline without
// knowing its shape.
struct PrefabTilingGuideDefinition {
  bw::core::PrefabTilingType type;
  std::string_view name;
  std::span<wp::Vector2 const> unitOutline;
};

[[nodiscard]] std::span<PrefabTilingGuideDefinition const>
prefabTilingGuideDefinitions();

[[nodiscard]] std::string_view prefabTilingGuideName(
    bw::core::PrefabTilingType type);

// Returns the closed guide's vertices in world space. Size is an edge length,
// so a square of size N spans -N/2 through +N/2 on both axes.
[[nodiscard]] std::vector<wp::Vector2> prefabTilingOutline(
    bw::core::PrefabTilingType type, float size);

}  // namespace editor
