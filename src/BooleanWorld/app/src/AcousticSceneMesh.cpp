#include "AcousticSceneMesh.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>

#include <core/ArrangementWorldData.h>

namespace bw::app {
namespace {

AcousticSceneVertex audioVertex(
    core::arr::FixedPointVertex const& vertex, float elevation) {
  return {core::arr::ToWorldCoordinate(vertex.x), elevation,
          -core::arr::ToWorldCoordinate(vertex.y)};
}

}  // namespace

AcousticSceneMesh ExportAcousticSceneMesh(
    core::ArrangementWorldData const& world,
    AcousticMaterialResolver const& resolveMaterial) {
  if (!resolveMaterial) {
    throw std::invalid_argument("Acoustic material resolver is required");
  }

  AcousticSceneMesh result;
  auto const& arrangement = world.getArrangement();
  auto const& horizontal = world.getTriangles();
  auto const& walls = world.getWalls();
  auto wallVertexCount = size_t{};
  auto wallTriangleCount = size_t{};
  for (auto const& wall : walls) {
    if (!wall.visible) continue;
    auto surface = core::arr::BuildArrangementWallSurface(arrangement, wall);
    wallVertexCount += surface.vertexCount;
    wallTriangleCount +=
        surface.vertexCount >= 3 ? surface.vertexCount - 2 : 0;
  }
  result.vertices.reserve(horizontal.size() * 6 + wallVertexCount);
  result.triangles.reserve(horizontal.size() * 2 + wallTriangleCount);

  std::unordered_map<std::string, uint32_t> materialIndices;
  auto materialIndexFor = [&](std::string const& subMaterialId) {
    auto const& preset = resolveMaterial(subMaterialId);
    if (auto found = materialIndices.find(preset.id);
        found != materialIndices.end()) {
      return found->second;
    }
    if (result.materials.size() >=
        size_t(std::numeric_limits<uint32_t>::max())) {
      throw std::overflow_error("Too many Acoustic presets in scene");
    }
    auto index = uint32_t(result.materials.size());
    result.materials.push_back(preset);
    materialIndices.emplace(preset.id, index);
    return index;
  };

  auto addTriangle = [&](AcousticSceneVertex const& a,
                         AcousticSceneVertex const& b,
                         AcousticSceneVertex const& c,
                         uint32_t materialIndex) {
    if (result.vertices.size() >
        size_t(std::numeric_limits<uint32_t>::max()) - 3) {
      throw std::overflow_error("Too many vertices in Acoustic scene");
    }
    auto first = uint32_t(result.vertices.size());
    result.vertices.push_back(a);
    result.vertices.push_back(b);
    result.vertices.push_back(c);
    result.triangles.push_back(
        {{first, first + 1, first + 2}, materialIndex});
  };

  for (auto const& triangle : horizontal) {
    auto const& face = arrangement.faces[triangle.face];
    auto const& properties = arrangement.palette[face.paletteIndex];
    std::array<AcousticSceneVertex, 3> floor;
    std::array<AcousticSceneVertex, 3> ceiling;
    for (size_t i = 0; i < 3; ++i) {
      auto const& vertex = arrangement.vertices[triangle.v[i]];
      floor[i] = audioVertex(vertex, triangle.floor.elevation[i]);
      ceiling[i] = audioVertex(vertex, triangle.ceiling.elevation[i]);
    }

    // Arrangement triangles face upward after conversion to audio space. Keep
    // that order for the floor and reverse it for the downward ceiling.
    addTriangle(floor[0], floor[1], floor[2],
                materialIndexFor(properties.floorMaterialId));
    addTriangle(ceiling[2], ceiling[1], ceiling[0],
                materialIndexFor(properties.ceilingMaterialId));
  }

  for (auto const& wall : walls) {
    if (!wall.visible) continue;
    auto surface = core::arr::BuildArrangementWallSurface(arrangement, wall);
    if (surface.vertexCount < 3) continue;
    auto const& properties = arrangement.palette[wall.paletteIndex];
    auto materialIndex = materialIndexFor(properties.wallMaterialId);

    if (result.vertices.size() >
        size_t(std::numeric_limits<uint32_t>::max()) - surface.vertexCount) {
      throw std::overflow_error("Too many vertices in Acoustic scene");
    }
    auto first = uint32_t(result.vertices.size());
    for (uint8_t corner = 0; corner < surface.vertexCount; ++corner) {
      auto const& vertex = surface.vertices[corner];
      result.vertices.push_back(
          {vertex.position.x, vertex.elevation, -vertex.position.y});
    }
    // The shared wall perimeter is ordered around the canonical front side;
    // triangulating it as a fan matches the renderer and emits exactly one
    // facet for a triangular Step segment and two for a quadrilateral.
    for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
      result.triangles.push_back(
          {{first, first + corner, first + corner + 1}, materialIndex});
    }
  }

  return result;
}

}  // namespace bw::app
