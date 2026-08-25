#pragma once

#include <array>
#include <cstddef>

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

}  // namespace editor
