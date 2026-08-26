#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <core/ArrangementWorldData.h>

namespace editor {

enum class PreviewSurface { None, Floor, Ceiling, Wall };

struct PreviewSurfaceHit {
  PreviewSurface surface{PreviewSurface::None};
  // Index into ArrangementWorldData::getWalls(); only meaningful for Wall.
  size_t wallIndex{};
  // Distance from the ray origin, in world units.
  float distance{};

  [[nodiscard]] bool hit() const {
    return surface != PreviewSurface::None;
  }
};

struct PreviewScenePick {
  // Index into ArrangementWorldData::getTriangles(); only meaningful for a
  // floor or ceiling hit. The legacy name is retained for call-site churn.
  size_t primitiveIndex{};
  PreviewSurfaceHit surfaceHit;

  [[nodiscard]] bool hit() const {
    return surfaceHit.hit();
  }
};

// Finds the nearest rendered Arrangement surface along a ray, for "what is
// the player looking at" in the 3D preview. The input is the same resolved,
// composited geometry WorldRenderer draws; no Primitive ordering or
// draw-order tie breaking is involved. Coordinates are (x, y ground-plane,
// z height), and the function is deliberately free of the graphics API.
// Surfaces are two-sided, matching the rendered preview.
[[nodiscard]] PreviewScenePick pickPreviewSceneSurface(
    bw::core::ArrangementWorldData const& worldData,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection);

// How the surface reads in the editor: "Floor", "Ceiling" or "Wall".
[[nodiscard]] std::string_view previewSurfaceName(PreviewSurface surface);

}  // namespace editor
