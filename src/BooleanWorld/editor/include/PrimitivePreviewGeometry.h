#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <core/MaterialDefinition.h>
#include <core/Primitive.h>

namespace editor {

class ProcMaterialLibrary;

struct PreviewVertex3 {
  float x{};
  float y{};
  float z{};
  // Face normal, in the same pre-swap (x, y-ground, z-height) space as the
  // position - submitVertex()/the material shader's vertex stage convert
  // both consistently.
  float nx{};
  float ny{};
  float nz{1.0f};
  float u{};
  float v{};
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
  bool resolved{};
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
    bw::core::Primitive const& primitive,
    ProcMaterialLibrary const* materials = nullptr);

}  // namespace editor
