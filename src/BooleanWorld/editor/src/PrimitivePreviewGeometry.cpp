#include "PrimitivePreviewGeometry.h"

namespace editor {
namespace {

PreviewVertex3 atHeight(wp::Vector2 const& point, float z) {
  return {point.x, point.y, z};
}

}  // namespace

PrimitivePreviewGeometry extrudePrimitiveForPreview(
    bw::core::Primitive const& primitive) {
  PrimitivePreviewGeometry result;
  auto const& properties = primitive.getProperties();
  result.floorMaterial = {
      properties.floorMaterialIndex, properties.floorMaterialDef.data};
  result.ceilingMaterial = {
      properties.ceilingMaterialIndex, properties.ceilingMaterialDef.data};
  result.wallMaterial = {
      properties.wallMaterialIndex, properties.wallMaterialDef.data};

  auto const& triangulation = primitive.getPickingTriangulation();
  result.floorTriangles.reserve(triangulation.tris.size());
  result.ceilingTriangles.reserve(triangulation.tris.size());
  for (auto const& triangle : triangulation.tris) {
    PreviewTriangle floor;
    PreviewTriangle ceiling;
    for (size_t i = 0; i < triangle.v.size(); ++i) {
      floor.vertices[i] = atHeight(triangle.v[i].p, properties.floorZ);
      // Reverse the ceiling winding so its visible side faces down into the
      // previewed volume.
      ceiling.vertices[triangle.v.size() - 1 - i] =
          atHeight(triangle.v[i].p, properties.ceilingZ);
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
        result.wallQuads.push_back({{{atHeight(first, properties.floorZ),
                                      atHeight(second, properties.floorZ),
                                      atHeight(second, properties.ceilingZ),
                                      atHeight(first, properties.ceilingZ)}}});
      }
    }
  }

  return result;
}

}  // namespace editor
