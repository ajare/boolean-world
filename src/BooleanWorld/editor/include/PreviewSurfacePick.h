#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "PrimitivePreviewGeometry.h"

namespace editor {

enum class PreviewSurface { None, Floor, Ceiling, Wall };

struct PreviewSurfaceHit {
  PreviewSurface surface{PreviewSurface::None};
  // Which wall Ring edge was hit; only meaningful for PreviewSurface::Wall.
  size_t wallIndex{};
  // Distance from the ray origin, in the same units as the geometry.
  float distance{};

  [[nodiscard]] bool hit() const {
    return surface != PreviewSurface::None;
  }
};

// Finds the nearest surface of one extruded Primitive along a ray, for
// "what is the player looking at" in the 3D preview. Works in the same
// (x, y ground-plane, z height) space as PrimitivePreviewGeometry, not the
// renderer's swapped 3D space, and is deliberately free of the graphics API.
//
// Surfaces are two-sided, matching the preview's disabled backface culling,
// so a Primitive is pickable from inside as well as outside.
[[nodiscard]] PreviewSurfaceHit pickPreviewSurface(
    PrimitivePreviewGeometry const& geometry,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection);

struct PreviewScenePick {
  // Index into the geometries passed in; only meaningful when hit().
  size_t primitiveIndex{};
  PreviewSurfaceHit surfaceHit;

  [[nodiscard]] bool hit() const {
    return surfaceHit.hit();
  }
};

// The surface the ray meets across a whole preview scene. Geometries must be
// given in the order they are drawn - ascending Primitive priority - because
// that is what decides the answer where two of them coincide.
//
// Primitives are extruded independently, so neighbours that share an edge
// produce exactly coplanar walls. The renderer draws them in order with
// GL_LEQUAL, so the last one drawn owns those pixels; this resolves such ties
// the same way, and therefore always names a surface that is actually
// visible rather than one buried behind its own duplicate.
[[nodiscard]] PreviewScenePick pickPreviewSceneSurface(
    std::vector<PrimitivePreviewGeometry const*> const& geometries,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection);

// How the surface reads in the editor: "Floor", "Ceiling" or "Wall".
[[nodiscard]] std::string_view previewSurfaceName(PreviewSurface surface);

}  // namespace editor
