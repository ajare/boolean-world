#pragma once

#include <vector>

#include <willpower/common/Vector2.h>

#include <core/DefinePrefabs.h>
#include <core/PrefabField.h>

namespace editor {

using PrefabPlacementOutline = std::vector<wp::Vector2>;

// Returns every contour of a Prefab translated from its authoring pivot to the
// centre of the target Tile. The outlines are editor-only placement geometry.
[[nodiscard]] std::vector<PrefabPlacementOutline> prefabPlacementOutlines(
    bw::core::Prefab const& prefab, bw::core::Tile tile,
    float clockwiseRotation = 0.0f);

}  // namespace editor
