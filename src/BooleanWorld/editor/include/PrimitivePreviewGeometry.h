#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <core/MaterialDefinition.h>
#include <core/Primitive.h>

namespace editor {

struct PreviewVertex3 {
  float x{};
  float y{};
  float z{};
};

struct PreviewTriangle {
  std::array<PreviewVertex3, 3> vertices;
};

struct PreviewWallQuad {
  std::array<PreviewVertex3, 4> vertices;
};

struct PreviewMaterial {
  uint32_t index{};
  bw::core::MaterialDefinitionData definition{};
};

// Render-ready extrusion of one raw Primitive. This type and its builder are
// deliberately independent of Arrangement, ImGui and the graphics API.
struct PrimitivePreviewGeometry {
  std::vector<PreviewTriangle> floorTriangles;
  std::vector<PreviewTriangle> ceilingTriangles;
  std::vector<PreviewWallQuad> wallQuads;
  PreviewMaterial floorMaterial;
  PreviewMaterial ceilingMaterial;
  PreviewMaterial wallMaterial;
};

[[nodiscard]] PrimitivePreviewGeometry extrudePrimitiveForPreview(
    bw::core::Primitive const& primitive);

}  // namespace editor
