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

// BuildArrangementWalls records the exact face whose Primitive owns each
// derived segment's wall material. Keep a fallback for hand-built fixtures
// that predate that field, using the segment midpoint rather than an unrelated
// plane sample at the World origin.
uint32_t wallFace(
    bw::core::arr::ArrangementResult const& arrangement,
    bw::core::arr::ArrangementWall const& wall) {
  if (wall.edge >= arrangement.edges.size()) {
    return ~0u;
  }
  if (wall.ownerFace < arrangement.faces.size()) {
    return wall.ownerFace;
  }
  auto const& edge = arrangement.edges[wall.edge];
  auto const& face0 = arrangement.faces[edge.face[0]];
  auto const& face1 = arrangement.faces[edge.face[1]];
  auto const& properties0 = arrangement.palette[face0.paletteIndex];
  auto const& properties1 = arrangement.palette[face1.paletteIndex];
  auto orientation =
      bw::core::arr::OrientArrangementWall(arrangement, wall);
  auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
  switch (wall.kind) {
    case bw::core::arr::ArrangementWallKind::Border:
      return face0.solid ? edge.face[0] : edge.face[1];
    case bw::core::arr::ArrangementWallKind::FloorStep:
      return properties0.floorZ.evaluate(midpoint) >
                     properties1.floorZ.evaluate(midpoint)
                 ? edge.face[0]
                 : edge.face[1];
    case bw::core::arr::ArrangementWallKind::CeilingStep:
      return properties0.ceilingZ.evaluate(midpoint) <
                     properties1.ceilingZ.evaluate(midpoint)
                 ? edge.face[0]
                 : edge.face[1];
  }
  return ~0u;
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
    for (auto const [surface, elevations] : {
             std::pair{PreviewSurface::Floor, &triangle.floor.elevation},
             std::pair{PreviewSurface::Ceiling, &triangle.ceiling.elevation}}) {
      std::array<Vector3, 3> vertices{
          horizontalVertex(arrangement, triangle.v[0], (*elevations)[0]),
          horizontalVertex(arrangement, triangle.v[1], (*elevations)[1]),
          horizontalVertex(arrangement, triangle.v[2], (*elevations)[2])};
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
    auto surface =
        bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
    auto vertex = [](bw::core::arr::ArrangementWallSurfaceVertex const& item) {
      return Vector3{item.position.x, item.position.y, item.elevation};
    };
    for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
      std::array<Vector3, 3> triangle{
          vertex(surface.vertices[0]), vertex(surface.vertices[corner]),
          vertex(surface.vertices[corner + 1])};
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

PreviewSurfaceOwner resolvePreviewSurfaceOwner(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick) {
  PreviewSurfaceOwner owner;
  if (!pick.hit()) {
    return owner;
  }

  auto const& arrangement = worldData.getArrangement();
  auto faceIndex = ~0u;
  if (pick.surfaceHit.surface == PreviewSurface::Wall) {
    auto const& walls = worldData.getWalls();
    if (pick.surfaceHit.wallIndex >= walls.size()) {
      return owner;
    }
    faceIndex = wallFace(arrangement, walls[pick.surfaceHit.wallIndex]);
  } else {
    auto const& triangles = worldData.getTriangles();
    if (pick.primitiveIndex >= triangles.size()) {
      return owner;
    }
    faceIndex = triangles[pick.primitiveIndex].face;
  }

  if (faceIndex >= arrangement.faces.size()) {
    return owner;
  }
  auto const& face = arrangement.faces[faceIndex];
  owner.faceIndex = faceIndex;
  owner.paletteIndex = face.paletteIndex;
  // Palette entry zero is the exterior and the empty faces that no Primitive
  // won; every other entry is one input Primitive, in input order.
  owner.primitiveListIndex =
      face.paletteIndex == 0 ? ~0u : uint32_t(face.paletteIndex - 1);
  return owner;
}

bw::core::SurfaceMaterialReference previewSurfaceMaterial(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick) {
  auto owner = resolvePreviewSurfaceOwner(worldData, pick);
  auto const& arrangement = worldData.getArrangement();
  if (!owner.valid() || owner.paletteIndex >= arrangement.palette.size()) {
    return {};
  }

  auto const& properties = arrangement.palette[owner.paletteIndex];
  switch (pick.surfaceHit.surface) {
    case PreviewSurface::Floor:
      return properties.floorMaterial;
    case PreviewSurface::Ceiling:
      return properties.ceilingMaterial;
    case PreviewSurface::Wall:
      return properties.wallMaterial;
    case PreviewSurface::None:
      break;
  }
  return {};
}

std::string previewSurfaceSubMaterialId(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick) {
  auto material = previewSurfaceMaterial(worldData, pick);
  return material.kind == bw::core::SurfaceMaterialKind::SubMaterial
             ? material.reference
             : std::string{};
}

std::string previewSurfaceEmbossPresetId(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick) {
  auto owner = resolvePreviewSurfaceOwner(worldData, pick);
  auto const& arrangement = worldData.getArrangement();
  if (!owner.valid() || owner.paletteIndex >= arrangement.palette.size()) {
    return {};
  }

  auto const& properties = arrangement.palette[owner.paletteIndex];
  switch (pick.surfaceHit.surface) {
    case PreviewSurface::Floor:
      return properties.floorEmbossPresetId;
    case PreviewSurface::Ceiling:
      return properties.ceilingEmbossPresetId;
    case PreviewSurface::Wall:
      return properties.wallEmbossPresetId;
    case PreviewSurface::None:
      break;
  }
  return {};
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
