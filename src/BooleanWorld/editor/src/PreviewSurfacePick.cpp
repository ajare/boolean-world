#include <cmath>

// MaterialRegistry sizes its tables with BW_MATERIAL_COUNT and
// BW_MATERIAL_PARAMS_MAX, so core's defines have to land first.
#include <core/Defines.h>

#include <common/MaterialRegistry.h>

#include "PreviewSurfacePick.h"

namespace editor {
namespace {

using Vector3 = std::array<float, 3>;

Vector3 toVector(PreviewVertex3 const& vertex) {
  return {vertex.x, vertex.y, vertex.z};
}

Vector3 subtract(Vector3 const& left, Vector3 const& right) {
  return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

Vector3 cross(Vector3 const& left, Vector3 const& right) {
  return {
      left[1] * right[2] - left[2] * right[1],
      left[2] * right[0] - left[0] * right[2],
      left[0] * right[1] - left[1] * right[0]};
}

float dot(Vector3 const& left, Vector3 const& right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

// Möller-Trumbore, without the sign test on the determinant so that both
// faces of a triangle are hit.
bool rayHitsTriangle(
    Vector3 const& origin,
    Vector3 const& direction,
    PreviewTriangle const& triangle,
    float& distance) {
  constexpr float epsilon = 1e-6f;

  auto a = toVector(triangle.vertices[0]);
  auto edge1 = subtract(toVector(triangle.vertices[1]), a);
  auto edge2 = subtract(toVector(triangle.vertices[2]), a);

  auto pvec = cross(direction, edge2);
  auto determinant = dot(edge1, pvec);
  // Parallel to, or degenerate in, the triangle's plane.
  if (std::abs(determinant) < epsilon) {
    return false;
  }

  auto inverseDeterminant = 1.0f / determinant;
  auto tvec = subtract(origin, a);
  auto u = dot(tvec, pvec) * inverseDeterminant;
  if (u < 0.0f || u > 1.0f) {
    return false;
  }

  auto qvec = cross(tvec, edge1);
  auto v = dot(direction, qvec) * inverseDeterminant;
  if (v < 0.0f || u + v > 1.0f) {
    return false;
  }

  auto t = dot(edge2, qvec) * inverseDeterminant;
  // Strictly in front of the origin, so a camera sitting exactly on a
  // surface does not pick it.
  if (t <= epsilon) {
    return false;
  }

  distance = t;
  return true;
}

void considerTriangles(
    Vector3 const& origin,
    Vector3 const& direction,
    std::vector<PreviewTriangle> const& triangles,
    PreviewSurface surface,
    PreviewSurfaceHit& nearest) {
  for (auto const& triangle : triangles) {
    float distance{};
    if (!rayHitsTriangle(origin, direction, triangle, distance)) {
      continue;
    }
    if (nearest.hit() && distance >= nearest.distance) {
      continue;
    }
    nearest.surface = surface;
    nearest.distance = distance;
  }
}

}  // namespace

PreviewSurfaceHit pickPreviewSurface(
    PrimitivePreviewGeometry const& geometry,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection) {
  PreviewSurfaceHit nearest;

  auto length = std::sqrt(dot(rayDirection, rayDirection));
  if (!(length > 0.0f)) {
    return nearest;
  }
  // Normalised, so the reported distance is a real distance and stays
  // comparable between calls made with differently scaled directions.
  Vector3 direction{
      rayDirection[0] / length, rayDirection[1] / length,
      rayDirection[2] / length};

  considerTriangles(
      rayOrigin, direction, geometry.floorTriangles, PreviewSurface::Floor,
      nearest);
  considerTriangles(
      rayOrigin, direction, geometry.ceilingTriangles, PreviewSurface::Ceiling,
      nearest);

  for (size_t index = 0; index < geometry.wallQuads.size(); ++index) {
    auto const& quad = geometry.wallQuads[index];
    // The same two triangles, in the same order, that the renderer lofts
    // this quad into.
    std::array<PreviewTriangle, 2> triangles{
        PreviewTriangle{
            {quad.vertices[0], quad.vertices[1], quad.vertices[2]}},
        PreviewTriangle{
            {quad.vertices[2], quad.vertices[3], quad.vertices[0]}}};
    for (auto const& triangle : triangles) {
      float distance{};
      if (!rayHitsTriangle(rayOrigin, direction, triangle, distance)) {
        continue;
      }
      if (nearest.hit() && distance >= nearest.distance) {
        continue;
      }
      nearest.surface = PreviewSurface::Wall;
      nearest.wallIndex = index;
      nearest.distance = distance;
    }
  }

  return nearest;
}

PreviewScenePick pickPreviewSceneSurface(
    std::vector<PrimitivePreviewGeometry const*> const& geometries,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection) {
  // Wide enough to catch coplanar duplicates through the accumulated float
  // error of two separate extrusions, far tighter than any real surface
  // separation in a level.
  constexpr float coplanarEpsilon = 1e-3f;

  PreviewScenePick nearest;
  for (size_t index = 0; index < geometries.size(); ++index) {
    if (!geometries[index]) {
      continue;
    }
    auto hit = pickPreviewSurface(*geometries[index], rayOrigin, rayDirection);
    if (!hit.hit()) {
      continue;
    }
    // Anything meaningfully further away loses. Anything nearer, or level
    // with the leader, takes over - so among coincident surfaces the last
    // drawn wins, exactly as GL_LEQUAL resolves them on screen.
    if (nearest.hit() &&
        hit.distance > nearest.surfaceHit.distance + coplanarEpsilon) {
      continue;
    }
    nearest.primitiveIndex = index;
    nearest.surfaceHit = hit;
  }
  return nearest;
}

PreviewMaterial const* previewSurfaceMaterial(
    PrimitivePreviewGeometry const& geometry, PreviewSurface surface) {
  switch (surface) {
    case PreviewSurface::Floor:
      return &geometry.floorMaterial;
    case PreviewSurface::Ceiling:
      return &geometry.ceilingMaterial;
    case PreviewSurface::Wall:
      return &geometry.wallMaterial;
    case PreviewSurface::None:
      break;
  }
  return nullptr;
}

std::string_view previewSurfaceName(PreviewSurface surface) {
  switch (surface) {
    case PreviewSurface::Floor:
      return "Floor";
    case PreviewSurface::Ceiling:
      return "Ceiling";
    case PreviewSurface::Wall:
      return "Wall";
    case PreviewSurface::None:
      break;
  }
  return "None";
}

std::string_view previewMaterialName(uint32_t materialIndex) {
  if (materialIndex >= bw::common::MaterialNames.size()) {
    return "Unknown";
  }
  return std::get<0>(bw::common::MaterialNames[materialIndex]);
}

}  // namespace editor
