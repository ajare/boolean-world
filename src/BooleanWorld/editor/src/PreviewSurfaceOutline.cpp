#include "PreviewSurfaceOutline.h"

#include <array>
#include <cstdint>

namespace editor {
namespace {

std::array<float, 3> rendererPoint(
    bw::core::arr::FixedPointVertex const& vertex, float height) {
  return {
      bw::core::arr::ToWorldCoordinate(vertex.x), height,
      -bw::core::arr::ToWorldCoordinate(vertex.y)};
}

}  // namespace

std::vector<std::array<float, 3>> previewSurfaceOutline(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& surface) {
  std::vector<std::array<float, 3>> segments;
  if (!surface.hit()) return segments;

  auto const& arrangement = worldData.getArrangement();
  auto appendLoop = [&](std::vector<uint32_t> const& loop, float height) {
    for (std::size_t index = 0; index < loop.size(); ++index) {
      auto const& from = arrangement.vertices[loop[index]];
      auto const& to = arrangement.vertices[loop[(index + 1) % loop.size()]];
      segments.push_back(rendererPoint(from, height));
      segments.push_back(rendererPoint(to, height));
    }
  };

  if (surface.surfaceHit.surface == PreviewSurface::Wall) {
    auto const& walls = worldData.getWalls();
    if (surface.surfaceHit.wallIndex >= walls.size()) return segments;
    auto const& wall = walls[surface.surfaceHit.wallIndex];
    if (wall.edge >= arrangement.edges.size()) return segments;
    auto const& edge = arrangement.edges[wall.edge];
    std::array<std::array<float, 3>, 4> quad{
        rendererPoint(arrangement.vertices[edge.v[0]], wall.minZ),
        rendererPoint(arrangement.vertices[edge.v[1]], wall.minZ),
        rendererPoint(arrangement.vertices[edge.v[1]], wall.maxZ),
        rendererPoint(arrangement.vertices[edge.v[0]], wall.maxZ)};
    for (std::size_t index = 0; index < quad.size(); ++index) {
      segments.push_back(quad[index]);
      segments.push_back(quad[(index + 1) % quad.size()]);
    }
    return segments;
  }

  auto const& triangles = worldData.getTriangles();
  if (surface.primitiveIndex >= triangles.size()) return segments;
  auto const faceIndex = triangles[surface.primitiveIndex].face;
  if (faceIndex >= arrangement.faces.size()) return segments;
  auto const& face = arrangement.faces[faceIndex];
  if (face.paletteIndex >= arrangement.palette.size()) return segments;
  auto const& properties = arrangement.palette[face.paletteIndex];
  auto const height = surface.surfaceHit.surface == PreviewSurface::Ceiling
                          ? properties.ceilingZ
                          : properties.floorZ;
  appendLoop(face.outerBoundaryVertices, height);
  for (auto const& hole : face.innerBoundaryVertices) appendLoop(hole, height);
  return segments;
}

}  // namespace editor
