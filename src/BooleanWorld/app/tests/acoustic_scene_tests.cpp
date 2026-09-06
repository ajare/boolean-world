#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>

#include "AcousticSceneMesh.h"

namespace {
using bw::app::AcousticMaterialResolver;
using bw::app::AcousticSceneMesh;
using bw::app::AcousticSceneVertex;
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;

struct Ray {
  AcousticSceneVertex origin;
  AcousticSceneVertex direction;
};

struct Hit {
  float distance{};
  std::string materialId;
};

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

AcousticSceneVertex operator+(
    AcousticSceneVertex const& a, AcousticSceneVertex const& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
AcousticSceneVertex operator-(
    AcousticSceneVertex const& a, AcousticSceneVertex const& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
AcousticSceneVertex operator*(AcousticSceneVertex const& a, float scale) {
  return {a.x * scale, a.y * scale, a.z * scale};
}
float dot(AcousticSceneVertex const& a, AcousticSceneVertex const& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
AcousticSceneVertex cross(
    AcousticSceneVertex const& a, AcousticSceneVertex const& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
Ray rayBetween(AcousticSceneVertex const& from, AcousticSceneVertex const& to) {
  auto direction = to - from;
  auto length = std::sqrt(dot(direction, direction));
  require(length > 0.0f, "test generated a zero-length ray");
  return {from, direction * (1.0f / length)};
}
float distanceBetween(
    AcousticSceneVertex const& from, AcousticSceneVertex const& to) {
  auto delta = to - from;
  return std::sqrt(dot(delta, delta));
}

std::optional<float> triangleIntersection(
    Ray const& ray, AcousticSceneVertex const& a,
    AcousticSceneVertex const& b, AcousticSceneVertex const& c,
    float minDistance, float maxDistance) {
  constexpr float Epsilon = 1.0e-6f;
  auto ab = b - a;
  auto ac = c - a;
  auto p = cross(ray.direction, ac);
  auto determinant = dot(ab, p);
  if (std::abs(determinant) <= Epsilon) return std::nullopt;
  auto inverse = 1.0f / determinant;
  auto offset = ray.origin - a;
  auto u = dot(offset, p) * inverse;
  if (u < -Epsilon || u > 1.0f + Epsilon) return std::nullopt;
  auto q = cross(offset, ab);
  auto v = dot(ray.direction, q) * inverse;
  if (v < -Epsilon || u + v > 1.0f + Epsilon) return std::nullopt;
  auto distance = dot(ac, q) * inverse;
  if (distance < minDistance || distance > maxDistance) return std::nullopt;
  return distance;
}

void consider(std::optional<Hit>& nearest, float distance,
              std::string const& materialId) {
  if (!nearest || distance < nearest->distance) {
    nearest = Hit{distance, materialId};
  }
}

std::optional<Hit> traceExportedMesh(
    AcousticSceneMesh const& mesh, Ray const& ray,
    float minDistance, float maxDistance) {
  std::optional<Hit> nearest;
  for (auto const& triangle : mesh.triangles) {
    auto hit = triangleIntersection(
        ray, mesh.vertices[triangle.vertices[0]],
        mesh.vertices[triangle.vertices[1]],
        mesh.vertices[triangle.vertices[2]], minDistance, maxDistance);
    if (hit) {
      consider(nearest, *hit, mesh.materials[triangle.materialIndex].id);
    }
  }
  return nearest;
}

bool pointInTriangle(
    float x, float y, bw::core::arr::ArrangementTriangle const& triangle,
    bw::core::arr::ArrangementResult const& arrangement) {
  auto point = AcousticSceneVertex{x, 0.0f, y};
  auto vertex = [&](uint32_t index) {
    auto const& fixed = arrangement.vertices[index];
    return AcousticSceneVertex{
        bw::core::arr::ToWorldCoordinate(fixed.x), 0.0f,
        bw::core::arr::ToWorldCoordinate(fixed.y)};
  };
  auto a = vertex(triangle.v[0]);
  auto b = vertex(triangle.v[1]);
  auto c = vertex(triangle.v[2]);
  auto v0 = b - a;
  auto v1 = c - a;
  auto v2 = point - a;
  auto denominator = v0.x * v1.z - v1.x * v0.z;
  if (std::abs(denominator) <= 1.0e-8f) return false;
  auto u = (v2.x * v1.z - v1.x * v2.z) / denominator;
  auto v = (v0.x * v2.z - v2.x * v0.z) / denominator;
  constexpr float Epsilon = 1.0e-5f;
  return u >= -Epsilon && v >= -Epsilon && u + v <= 1.0f + Epsilon;
}

// Test-only 2.5D reference: horizontal surfaces are intersected analytically,
// and visible Arrangement walls are found by marching the ray in the World
// plane before evaluating its elevation at each crossing.
std::optional<Hit> traceArrangementReference(
    ArrangementWorldData const& world, AcousticMaterialResolver const& resolve,
    Ray const& ray, float minDistance, float maxDistance) {
  std::optional<Hit> nearest;
  auto const& arrangement = world.getArrangement();

  if (std::abs(ray.direction.y) > 1.0e-7f) {
    for (auto const& triangle : world.getTriangles()) {
      auto const& properties = arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
      auto horizontal = [&](float elevation, std::string const& material) {
        auto distance = (elevation - ray.origin.y) / ray.direction.y;
        if (distance < minDistance || distance > maxDistance) return;
        auto point = ray.origin + ray.direction * distance;
        // Audio -Z maps back to authored +Y.
        if (pointInTriangle(point.x, -point.z, triangle, arrangement)) {
          consider(nearest, distance, resolve(material).id);
        }
      };
      horizontal(properties.floorZ, properties.floorMaterialId);
      horizontal(properties.ceilingZ, properties.ceilingMaterialId);
    }
  }

  auto worldOriginX = ray.origin.x;
  auto worldOriginY = -ray.origin.z;
  auto worldDirectionX = ray.direction.x;
  auto worldDirectionY = -ray.direction.z;
  for (auto const& wall : world.getWalls()) {
    if (!wall.visible) continue;
    auto const& edge = arrangement.edges[wall.edge];
    auto const& fixedA = arrangement.vertices[edge.v[0]];
    auto const& fixedB = arrangement.vertices[edge.v[1]];
    auto ax = bw::core::arr::ToWorldCoordinate(fixedA.x);
    auto ay = bw::core::arr::ToWorldCoordinate(fixedA.y);
    auto spanX = bw::core::arr::ToWorldCoordinate(fixedB.x) - ax;
    auto spanY = bw::core::arr::ToWorldCoordinate(fixedB.y) - ay;
    auto determinant = worldDirectionX * spanY - spanX * worldDirectionY;
    if (std::abs(determinant) <= 1.0e-7f) continue;
    auto offsetX = ax - worldOriginX;
    auto offsetY = ay - worldOriginY;
    auto distance = (offsetX * spanY - spanX * offsetY) / determinant;
    auto alongWall =
        (offsetX * worldDirectionY - worldDirectionX * offsetY) /
        determinant;
    if (distance < minDistance || distance > maxDistance ||
        alongWall < 0.0f || alongWall > 1.0f) {
      continue;
    }
    auto elevation = ray.origin.y + ray.direction.y * distance;
    if (elevation < wall.minZ || elevation > wall.maxZ) continue;
    auto const& properties = arrangement.palette[wall.paletteIndex];
    consider(nearest, distance,
             resolve(properties.wallMaterialId).id);
  }
  return nearest;
}

// Independent brute-force reference: rebuild the Arrangement's surfaces as a
// triangle soup and apply an ordinary 3D ray/triangle test to every facet.
std::optional<Hit> traceArrangementBruteForce(
    ArrangementWorldData const& world, AcousticMaterialResolver const& resolve,
    Ray const& ray, float minDistance, float maxDistance) {
  struct Facet {
    AcousticSceneVertex a, b, c;
    std::string materialId;
  };
  std::vector<Facet> facets;
  auto const& arrangement = world.getArrangement();
  auto horizontalVertex = [&](uint32_t index, float elevation) {
    auto const& vertex = arrangement.vertices[index];
    return AcousticSceneVertex{
        bw::core::arr::ToWorldCoordinate(vertex.x), elevation,
        -bw::core::arr::ToWorldCoordinate(vertex.y)};
  };
  for (auto const& triangle : world.getTriangles()) {
    auto const& properties = arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    auto a = horizontalVertex(triangle.v[0], properties.floorZ);
    auto b = horizontalVertex(triangle.v[1], properties.floorZ);
    auto c = horizontalVertex(triangle.v[2], properties.floorZ);
    facets.push_back({a, b, c, resolve(properties.floorMaterialId).id});
    a.y = properties.ceilingZ;
    b.y = properties.ceilingZ;
    c.y = properties.ceilingZ;
    facets.push_back({a, b, c, resolve(properties.ceilingMaterialId).id});
  }
  for (auto const& wall : world.getWalls()) {
    if (!wall.visible) continue;
    auto const& edge = arrangement.edges[wall.edge];
    auto make = [&](uint32_t index, float elevation) {
      auto const& vertex = arrangement.vertices[index];
      return AcousticSceneVertex{
          bw::core::arr::ToWorldCoordinate(vertex.x), elevation,
          -bw::core::arr::ToWorldCoordinate(vertex.y)};
    };
    auto bottom0 = make(edge.v[0], wall.minZ);
    auto bottom1 = make(edge.v[1], wall.minZ);
    auto top0 = make(edge.v[0], wall.maxZ);
    auto top1 = make(edge.v[1], wall.maxZ);
    auto material = resolve(
                        arrangement.palette[wall.paletteIndex].wallMaterialId)
                        .id;
    facets.push_back({top1, bottom1, bottom0, material});
    facets.push_back({bottom0, top0, top1, material});
  }

  std::optional<Hit> nearest;
  for (auto const& facet : facets) {
    if (auto distance = triangleIntersection(
            ray, facet.a, facet.b, facet.c, minDistance, maxDistance)) {
      consider(nearest, *distance, facet.materialId);
    }
  }
  return nearest;
}

Contour rectangle(int x0, int y0, int x1, int y1) {
  constexpr int64_t U = bw::core::arr::FixedPointUnitsPerWorldUnit;
  return {{x0 * U, y0 * U}, {x1 * U, y0 * U}, {x1 * U, y1 * U}, {x0 * U, y1 * U}};
}

PrimitivePropertySet properties(
    float floor, float ceiling, std::string const& prefix) {
  PrimitivePropertySet result;
  result.floorZ = floor;
  result.ceilingZ = ceiling;
  result.floorMaterialId = prefix + ".floor";
  result.ceilingMaterialId = prefix + ".ceiling";
  result.wallMaterialId = prefix + ".wall";
  return result;
}

std::shared_ptr<ArrangementWorldData> makeWorld() {
  ArrangementPrimitive left{
      {rectangle(0, 0, 10, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 0, 1, properties(0.0f, 20.0f, "left")};
  std::vector<std::optional<bool>> rightVisibility(4, std::nullopt);
  // Hide the x=20 Border while leaving its default collision intact: sound
  // follows the rendered surface, not actor collision.
  rightVisibility[1] = false;
  ArrangementPrimitive right{
      {rectangle(10, 0, 20, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 1, 2, properties(5.0f, 15.0f, "right"), {}, {rightVisibility}};
  return std::make_shared<ArrangementWorldData>(
      bw::core::arr::BuildArrangement({left, right}),
      wp::BoundingBox({-5.0f, -5.0f}, {30.0f, 20.0f}), 4.0f);
}

std::unordered_map<std::string, AcousticPreset> makePresets() {
  std::unordered_map<std::string, AcousticPreset> result;
  for (auto side : {std::string("left"), std::string("right")}) {
    for (auto surface : {std::string("floor"), std::string("ceiling"),
                         std::string("wall")}) {
      auto id = side + "." + surface;
      result.emplace(id, AcousticPreset{id, id, {0.1f, 0.2f, 0.3f}, 0.05f, {0.01f, 0.02f, 0.03f}});
    }
  }
  return result;
}

void requireEquivalent(
    std::optional<Hit> const& actual, std::optional<Hit> const& expected,
    std::string const& context) {
  require(bool(actual) == bool(expected), context + ": hit presence differs");
  if (!actual) return;
  require(std::abs(actual->distance - expected->distance) < 0.002f,
          context + ": hit distance differs");
  require(actual->materialId == expected->materialId,
          context + ": Acoustic preset differs");
}

void exportHasEverySurfaceAndResolvedMaterial() {
  auto world = makeWorld();
  auto presets = makePresets();
  AcousticMaterialResolver resolver = [&](std::string const& id)
      -> AcousticPreset const& { return presets.at(id); };
  auto mesh = bw::app::ExportAcousticSceneMesh(*world, resolver);
  auto visibleWalls = std::count_if(
      world->getWalls().begin(), world->getWalls().end(),
      [](auto const& wall) { return wall.visible; });
  require(mesh.triangles.size() ==
              world->getTriangles().size() * 2 + size_t(visibleWalls) * 2,
          "export did not contain one floor, ceiling, and visible wall quad");
  require(mesh.vertices.size() ==
              world->getTriangles().size() * 6 + size_t(visibleWalls) * 4,
          "exported wall quads did not retain their four shared vertices");
  require(mesh.materials.size() == presets.size(),
          "export material table did not de-duplicate resolved presets");
  for (auto const& triangle : mesh.triangles) {
    require(triangle.materialIndex < mesh.materials.size(),
            "exported triangle has an invalid material index");
  }

  auto triangleNormal = [&](size_t index) {
    auto const& triangle = mesh.triangles[index];
    return cross(mesh.vertices[triangle.vertices[1]] -
                     mesh.vertices[triangle.vertices[0]],
                 mesh.vertices[triangle.vertices[2]] -
                     mesh.vertices[triangle.vertices[0]]);
  };
  for (size_t i = 0; i < world->getTriangles().size(); ++i) {
    require(triangleNormal(i * 2).y > 0.0f,
            "exported floor winding does not face upward");
    require(triangleNormal(i * 2 + 1).y < 0.0f,
            "exported ceiling winding does not face downward");
  }
  auto exportedWallTriangle = world->getTriangles().size() * 2;
  for (auto const& wall : world->getWalls()) {
    if (!wall.visible) continue;
    auto orientation =
        bw::core::arr::OrientArrangementWall(world->getArrangement(), wall);
    AcousticSceneVertex expected{
        orientation.normal.x, 0.0f, -orientation.normal.y};
    require(dot(triangleNormal(exportedWallTriangle), expected) > 0.0f &&
                dot(triangleNormal(exportedWallTriangle + 1), expected) > 0.0f,
            "exported wall winding does not match its canonical front side");
    exportedWallTriangle += 2;
  }
}

void propertyRaysAgree() {
  auto world = makeWorld();
  auto presets = makePresets();
  AcousticMaterialResolver resolver = [&](std::string const& id)
      -> AcousticPreset const& { return presets.at(id); };
  auto mesh = bw::app::ExportAcousticSceneMesh(*world, resolver);
  std::mt19937 random(398);
  std::uniform_real_distribution<float> insideX(0.5f, 9.5f);
  std::uniform_real_distribution<float> insideY(0.5f, 9.5f);
  std::uniform_real_distribution<float> insideZ(0.5f, 19.5f);

  // Clear, same-face segments remain wholly inside the left room.
  for (int i = 0; i < 250; ++i) {
    AcousticSceneVertex from{insideX(random), insideZ(random), -insideY(random)};
    AcousticSceneVertex to{insideX(random), insideZ(random), -insideY(random)};
    auto ray = rayBetween(from, to);
    auto maxDistance = distanceBetween(from, to);
    require(!traceArrangementReference(*world, resolver, ray, 0.001f,
                                       maxDistance - 0.001f),
            "clear same-face property ray hit Arrangement geometry");
    require(!traceExportedMesh(mesh, ray, 0.001f, maxDistance - 0.001f),
            "clear same-face property ray hit exported geometry");
  }

  // The shared edge has a 0..5 FloorStep governed by right.wall.
  std::uniform_real_distribution<float> lowElevation(0.25f, 4.75f);
  for (int i = 0; i < 250; ++i) {
    AcousticSceneVertex from{8.0f, lowElevation(random), -insideY(random)};
    AcousticSceneVertex to{12.0f, from.y, from.z};
    auto ray = rayBetween(from, to);
    Hit expected{2.0f, "right.wall"};
    requireEquivalent(
        traceArrangementReference(*world, resolver, ray, 0.001f, 4.0f),
        expected, "rendered-wall reference");
    requireEquivalent(traceExportedMesh(mesh, ray, 0.001f, 4.0f), expected,
                      "rendered-wall export");
  }

  // A colliding Border hidden by its authored visibility override is absent
  // from both the structural reference and exported acoustic mesh.
  {
    AcousticSceneVertex from{18.0f, 10.0f, -5.0f};
    AcousticSceneVertex to{22.0f, 10.0f, -5.0f};
    auto ray = rayBetween(from, to);
    require(!traceArrangementReference(*world, resolver, ray, 0.001f, 4.0f),
            "hidden colliding wall blocked the Arrangement reference");
    require(!traceExportedMesh(mesh, ray, 0.001f, 4.0f),
            "hidden colliding wall was exported to the Acoustic scene");
  }

  // Vertical rays resolve the floor and ceiling independently.
  for (int i = 0; i < 250; ++i) {
    AcousticSceneVertex origin{insideX(random), insideZ(random),
                               -insideY(random)};
    auto down = Ray{origin, {0.0f, -1.0f, 0.0f}};
    auto up = Ray{origin, {0.0f, 1.0f, 0.0f}};
    Hit floor{origin.y, "left.floor"};
    Hit ceiling{20.0f - origin.y, "left.ceiling"};
    requireEquivalent(
        traceArrangementReference(*world, resolver, down, 0.001f, 30.0f),
        floor, "floor reference");
    requireEquivalent(traceExportedMesh(mesh, down, 0.001f, 30.0f), floor,
                      "floor export");
    requireEquivalent(
        traceArrangementReference(*world, resolver, up, 0.001f, 30.0f),
        ceiling, "ceiling reference");
    requireEquivalent(traceExportedMesh(mesh, up, 0.001f, 30.0f), ceiling,
                      "ceiling export");
  }

  // Broad random coverage compares the structural reference against a direct
  // triangle implementation over the Arrangement, then checks the export.
  std::uniform_real_distribution<float> broadX(-3.0f, 23.0f);
  std::uniform_real_distribution<float> broadY(-3.0f, 13.0f);
  std::uniform_real_distribution<float> broadZ(-3.0f, 23.0f);
  for (int i = 0; i < 2000; ++i) {
    AcousticSceneVertex from{broadX(random), broadZ(random), -broadY(random)};
    AcousticSceneVertex to{broadX(random), broadZ(random), -broadY(random)};
    auto ray = rayBetween(from, to);
    auto maxDistance = distanceBetween(from, to);
    auto reference = traceArrangementReference(
        *world, resolver, ray, 0.001f, maxDistance);
    auto brute = traceArrangementBruteForce(
        *world, resolver, ray, 0.001f, maxDistance);
    auto exported = traceExportedMesh(mesh, ray, 0.001f, maxDistance);
    requireEquivalent(reference, brute, "Arrangement reference vs brute force");
    requireEquivalent(exported, reference, "export vs Arrangement reference");
  }
}

}  // namespace

int main() {
  try {
    exportHasEverySurfaceAndResolvedMaterial();
    propertyRaysAgree();
    std::cout << "Acoustic scene export coverage passed\n";
  } catch (std::exception const& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
