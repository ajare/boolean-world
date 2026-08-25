#include <cmath>

#include "PrimitivePreviewGeometry.h"
#include "ProcMaterialLibrary.h"

namespace editor {
namespace {

PreviewVertex3 atHeight(
    wp::Vector2 const& point, float z, float nx, float ny, float nz) {
  // World-space planar texture coordinates - the material shader mostly
  // drives its procedural coordinates from world position, so this is only
  // meaningful for the TEX1 detail sample.
  return {point.x, point.y, z, nx, ny, nz, point.x, point.y};
}

}  // namespace

PrimitivePreviewGeometry extrudePrimitiveForPreview(
    bw::core::Primitive const& primitive,
    ProcMaterialLibrary const* materials) {
  PrimitivePreviewGeometry result;
  auto const& properties = primitive.getProperties();
  auto resolve = [materials](std::string const& id) {
    PreviewMaterial material;
    if (!materials) return material;
    auto const* subMaterial = materials->findSubMaterial(id);
    if (!subMaterial) return material;
    material.index = subMaterial->materialIndex;
    material.definition.params = {};
    for (size_t i = 0; i < subMaterial->paramValues.size(); ++i) {
      material.definition.params[i] = subMaterial->paramValues[i];
    }
    material.definition.baseColour = subMaterial->baseColour;
    return material;
  };
  result.floorMaterial = resolve(properties.floorMaterialId);
  result.ceilingMaterial = resolve(properties.ceilingMaterialId);
  result.wallMaterial = resolve(properties.wallMaterialId);

  auto const& triangulation = primitive.getPickingTriangulation();
  result.floorTriangles.reserve(triangulation.tris.size());
  result.ceilingTriangles.reserve(triangulation.tris.size());
  for (auto const& triangle : triangulation.tris) {
    PreviewTriangle floor;
    PreviewTriangle ceiling;
    for (size_t i = 0; i < triangle.v.size(); ++i) {
      floor.vertices[i] =
          atHeight(triangle.v[i].p, properties.floorZ, 0.0f, 0.0f, 1.0f);
      // Reverse the ceiling winding so its visible side faces down into the
      // previewed volume.
      ceiling.vertices[triangle.v.size() - 1 - i] = atHeight(
          triangle.v[i].p, properties.ceilingZ, 0.0f, 0.0f, -1.0f);
    }
    result.floorTriangles.push_back(floor);
    result.ceilingTriangles.push_back(ceiling);
  }

  // getVertices() is the transformed, world-space derived form. Every
  // ComplexPolygon contains its filled Ring and direct Hole Rings; nested
  // Islands occur as subsequent ComplexPolygons, so this visits each owned
  // Ring exactly once at every containment depth.
  for (auto const& polygon : primitive.getVertices()) {
    for (auto const& ring : polygon) {
      if (ring.size() < 2) {
        continue;
      }
      result.wallQuads.reserve(result.wallQuads.size() + ring.size());
      for (size_t i = 0; i < ring.size(); ++i) {
        auto const& first = ring[i].p;
        auto const& second = ring[(i + 1) % ring.size()].p;

        // A single flat quad stands in for both faces of the wall (no
        // player-facing side selection, unlike the game's real wall
        // rendering), so pick one consistent outward-perpendicular
        // convention rather than none at all.
        auto edge = second - first;
        auto length = std::sqrt(edge.x * edge.x + edge.y * edge.y);
        float nx = 0.0f, ny = 0.0f;
        if (length > 1e-6f) {
          nx = edge.y / length;
          ny = -edge.x / length;
        }

        PreviewWallQuad quad{{{
            atHeight(first, properties.floorZ, nx, ny, 0.0f),
            atHeight(second, properties.floorZ, nx, ny, 0.0f),
            atHeight(second, properties.ceilingZ, nx, ny, 0.0f),
            atHeight(first, properties.ceilingZ, nx, ny, 0.0f),
        }}};
        quad.vertices[0].u = 0.0f;
        quad.vertices[0].v = 0.0f;
        quad.vertices[1].u = length;
        quad.vertices[1].v = 0.0f;
        quad.vertices[2].u = length;
        quad.vertices[2].v = properties.ceilingZ - properties.floorZ;
        quad.vertices[3].u = 0.0f;
        quad.vertices[3].v = properties.ceilingZ - properties.floorZ;
        result.wallQuads.push_back(quad);
      }
    }
  }

  return result;
}

}  // namespace editor
