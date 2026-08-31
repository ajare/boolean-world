#include "PrefabPlacementPreview.h"

namespace editor {

std::vector<PrefabPlacementOutline> prefabPlacementOutlines(
    bw::core::Prefab const& prefab, bw::core::Tile tile,
    float clockwiseRotation) {
  auto const side =
      static_cast<float>(bw::core::prefabTileSide(tile.size));
  auto const tileCentre = wp::Vector2{
      (static_cast<float>(tile.x) + 0.5f) * side,
      (static_cast<float>(tile.y) + 0.5f) * side};

  std::vector<PrefabPlacementOutline> outlines;
  for (auto const* primitive : prefab.getPrimitives()) {
    for (auto const& complexPolygon : primitive->getVertices()) {
      for (auto const& contour : complexPolygon) {
        auto& outline = outlines.emplace_back();
        outline.reserve(contour.size());
        for (auto const& vertex : contour) {
          outline.push_back(
              vertex.p.rotatedClockwiseCopy(clockwiseRotation) + tileCentre);
        }
      }
    }
  }
  return outlines;
}

}  // namespace editor
