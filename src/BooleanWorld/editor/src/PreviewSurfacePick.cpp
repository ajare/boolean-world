#include <cmath>

#include "PreviewSurfacePick.h"

namespace editor {
namespace {

using Vector3 = std::array<float, 3>;

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
    std::array<Vector3, 3> const& triangle,
    float& distance) {
  constexpr float epsilon = 1e-6f;

  auto edge1 = subtract(triangle[1], triangle[0]);
  auto edge2 = subtract(triangle[2], triangle[0]);
  auto pvec = cross(direction, edge2);
  auto determinant = dot(edge1, pvec);
  if (std::abs(determinant) < epsilon) {
    return false;
  }

  auto inverseDeterminant = 1.0f / determinant;
  auto tvec = subtract(origin, triangle[0]);
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
  if (t <= epsilon) {
    return false;
  }

  distance = t;
  return true;
}

Vector3 horizontalVertex(
    bw::core::arr::ArrangementResult const& arrangement,
    uint32_t vertexIndex,
    float z) {
  auto const& vertex = arrangement.vertices[vertexIndex];
  return {
      bw::core::arr::ToWorldCoordinate(vertex.x),
      bw::core::arr::ToWorldCoordinate(vertex.y), z};
}

}  // namespace

PreviewScenePick pickPreviewSceneSurface(
    bw::core::ArrangementWorldData const& worldData,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection) {
  PreviewScenePick nearest;
  auto length = std::sqrt(dot(rayDirection, rayDirection));
  if (!(length > 0.0f)) {
    return nearest;
  }
  Vector3 direction{
      rayDirection[0] / length, rayDirection[1] / length,
      rayDirection[2] / length};

  auto const& arrangement = worldData.getArrangement();
  auto const& triangles = worldData.getTriangles();
  for (size_t index = 0; index < triangles.size(); ++index) {
    auto const& triangle = triangles[index];
    auto const& properties =
        arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    for (auto const [surface, z] : {
             std::pair{PreviewSurface::Floor, properties.floorZ},
             std::pair{PreviewSurface::Ceiling, properties.ceilingZ}}) {
      std::array<Vector3, 3> vertices{
          horizontalVertex(arrangement, triangle.v[0], z),
          horizontalVertex(arrangement, triangle.v[1], z),
          horizontalVertex(arrangement, triangle.v[2], z)};
      float distance{};
      if (!rayHitsTriangle(rayOrigin, direction, vertices, distance) ||
          (nearest.hit() && distance >= nearest.surfaceHit.distance)) {
        continue;
      }
      nearest.primitiveIndex = index;
      nearest.surfaceHit.surface = surface;
      nearest.surfaceHit.distance = distance;
    }
  }

  auto const& walls = worldData.getWalls();
  for (size_t index = 0; index < walls.size(); ++index) {
    auto const& wall = walls[index];
    if (!wall.visible) {
      continue;
    }
    auto const& edge = arrangement.edges[wall.edge];
    auto bottom0 = horizontalVertex(arrangement, edge.v[0], wall.minZ);
    auto bottom1 = horizontalVertex(arrangement, edge.v[1], wall.minZ);
    auto top0 = horizontalVertex(arrangement, edge.v[0], wall.maxZ);
    auto top1 = horizontalVertex(arrangement, edge.v[1], wall.maxZ);
    std::array<std::array<Vector3, 3>, 2> wallTriangles{
        std::array<Vector3, 3>{bottom0, bottom1, top1},
        std::array<Vector3, 3>{top1, top0, bottom0}};
    for (auto const& triangle : wallTriangles) {
      float distance{};
      if (!rayHitsTriangle(rayOrigin, direction, triangle, distance) ||
          (nearest.hit() && distance >= nearest.surfaceHit.distance)) {
        continue;
      }
      nearest.surfaceHit.surface = PreviewSurface::Wall;
      nearest.surfaceHit.wallIndex = index;
      nearest.surfaceHit.distance = distance;
    }
  }

  return nearest;
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

}  // namespace editor
