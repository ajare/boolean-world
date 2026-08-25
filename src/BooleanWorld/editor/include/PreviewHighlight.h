#pragma once

#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/mat4x4.hpp>
#pragma warning(pop)

#include "PrimitivePreviewGeometry.h"

namespace editor {

// Outlines the borders of the given triangles in thick, bright yellow, to
// mark the surface the viewer is looking at in the 3D preview.
//
// Triangles arrive in PrimitivePreviewGeometry's (x, y ground-plane, z
// height) space and are swapped to the renderer's 3D space here, exactly as
// the material pass does. Draws through the fixed-function pipeline, which
// this compatibility-profile context supports and which needs no vertex
// state of its own - so it is unaffected by whatever buffers ImGui has
// bound around the enclosing draw callback.
void drawPreviewHighlight(
    std::vector<PreviewTriangle> const& triangles,
    glm::mat4 const& viewTransform,
    glm::mat4 const& projectionTransform);

}  // namespace editor
