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
  auto visibleWallCount = size_t{};
  for (auto const& wall : walls) {
    if (wall.visible) ++visibleWallCount;
  }
  result.vertices.reserve(horizontal.size() * 6 + visibleWallCount * 4);
  result.triangles.reserve(horizontal.size() * 2 + visibleWallCount * 2);

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
      floor[i] = audioVertex(vertex, properties.floorZ);
      ceiling[i] = audioVertex(vertex, properties.ceilingZ);
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
    auto const orientation = core::arr::OrientArrangementWall(arrangement, wall);
    auto const& properties = arrangement.palette[wall.paletteIndex];
    auto materialIndex = materialIndexFor(properties.wallMaterialId);
    AcousticSceneVertex const bottom0{
        orientation.v0.x, wall.minZ, -orientation.v0.y};
    AcousticSceneVertex const bottom1{
        orientation.v1.x, wall.minZ, -orientation.v1.y};
    AcousticSceneVertex const top1{
        orientation.v1.x, wall.maxZ, -orientation.v1.y};
    AcousticSceneVertex const top0{
        orientation.v0.x, wall.maxZ, -orientation.v0.y};

    // Match the visible wall quad's diagonal and front-face winding while
    // retaining one four-vertex quad rather than two disconnected facets.
    if (result.vertices.size() >
        size_t(std::numeric_limits<uint32_t>::max()) - 4) {
      throw std::overflow_error("Too many vertices in Acoustic scene");
    }
    auto first = uint32_t(result.vertices.size());
    result.vertices.push_back(bottom0);
    result.vertices.push_back(bottom1);
    result.vertices.push_back(top1);
    result.vertices.push_back(top0);
    result.triangles.push_back(
        {{first + 2, first + 1, first}, materialIndex});
    result.triangles.push_back(
        {{first, first + 3, first + 2}, materialIndex});
  }

  return result;
}

}  // namespace bw::app
