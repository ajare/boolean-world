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
#include <type_traits>
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

// Test-only 2.5D reference: planar surfaces are intersected analytically, and
// visible Arrangement walls are found by marching the ray in the World plane
// before evaluating their boundaries at each crossing.
std::optional<Hit> traceArrangementReference(
    ArrangementWorldData const& world, AcousticMaterialResolver const& resolve,
    Ray const& ray, float minDistance, float maxDistance) {
  std::optional<Hit> nearest;
  auto const& arrangement = world.getArrangement();

  for (auto const& triangle : world.getTriangles()) {
    auto const& properties =
        arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    auto planar = [&](
                      bw::core::arr::ArrangementTriangleSurface const& surface,
                      bw::core::SurfaceMaterialReference const& material) {
      auto const& fixed = arrangement.vertices[triangle.v[0]];
      AcousticSceneVertex pointOnPlane{
          bw::core::arr::ToWorldCoordinate(fixed.x), surface.elevation[0],
          -bw::core::arr::ToWorldCoordinate(fixed.y)};
      AcousticSceneVertex normal{
          surface.normal[0], surface.normal[2], -surface.normal[1]};
      auto denominator = dot(ray.direction, normal);
      if (std::abs(denominator) <= 1.0e-7f) return;
      auto distance = dot(pointOnPlane - ray.origin, normal) / denominator;
      if (distance < minDistance || distance > maxDistance) return;
      auto point = ray.origin + ray.direction * distance;
      // Audio -Z maps back to authored +Y.
      if (pointInTriangle(point.x, -point.z, triangle, arrangement)) {
        consider(nearest, distance, resolve(material).id);
      }
    };
    planar(triangle.floor, properties.floorMaterial);
    planar(triangle.ceiling, properties.ceilingMaterial);
  }

  auto worldOriginX = ray.origin.x;
  auto worldOriginY = -ray.origin.z;
  auto worldDirectionX = ray.direction.x;
  auto worldDirectionY = -ray.direction.z;
  for (auto const& wall : world.getWalls()) {
    if (!wall.visible) continue;
    auto orientation =
        bw::core::arr::OrientArrangementWall(arrangement, wall);
    auto ax = orientation.v0.x;
    auto ay = orientation.v0.y;
    auto spanX = orientation.v1.x - ax;
    auto spanY = orientation.v1.y - ay;
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
    auto bottom = orientation.bottomZ[0] +
                  (orientation.bottomZ[1] - orientation.bottomZ[0]) *
                      alongWall;
    auto top = orientation.topZ[0] +
               (orientation.topZ[1] - orientation.topZ[0]) * alongWall;
    if (elevation < bottom || elevation > top) continue;
    auto const& properties = arrangement.palette[wall.paletteIndex];
    consider(nearest, distance,
             resolve(properties.wallMaterial).id);
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
    auto const& properties =
        arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    auto a = horizontalVertex(triangle.v[0], triangle.floor.elevation[0]);
    auto b = horizontalVertex(triangle.v[1], triangle.floor.elevation[1]);
    auto c = horizontalVertex(triangle.v[2], triangle.floor.elevation[2]);
    facets.push_back({a, b, c, resolve(properties.floorMaterial).id});
    a = horizontalVertex(triangle.v[0], triangle.ceiling.elevation[0]);
    b = horizontalVertex(triangle.v[1], triangle.ceiling.elevation[1]);
    c = horizontalVertex(triangle.v[2], triangle.ceiling.elevation[2]);
    facets.push_back({a, b, c, resolve(properties.ceilingMaterial).id});
  }
  for (auto const& wall : world.getWalls()) {
    if (!wall.visible) continue;
    auto surface =
        bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
    auto make = [](bw::core::arr::ArrangementWallSurfaceVertex const& vertex) {
      return AcousticSceneVertex{
          vertex.position.x, vertex.elevation, -vertex.position.y};
    };
    auto material = resolve(
                        arrangement.palette[wall.paletteIndex].wallMaterial)
                        .id;
    for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
      facets.push_back(
          {make(surface.vertices[0]), make(surface.vertices[corner]),
           make(surface.vertices[corner + 1]), material});
    }
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
  result.floorMaterial = bw::core::SurfaceMaterialReference::subMaterial(prefix + ".floor");
  result.ceilingMaterial = bw::core::SurfaceMaterialReference::subMaterial(prefix + ".ceiling");
  result.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial(prefix + ".wall");
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

std::shared_ptr<ArrangementWorldData> makeSlopedWorld() {
  auto sloped = properties(1.0f, 20.0f, "slope");
  sloped.floorZ.gradient = {0.5f, -0.2f};
  sloped.ceilingZ.gradient = {-0.1f, 0.3f};
  ArrangementPrimitive room{
      {rectangle(0, 0, 10, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 0, 1, sloped};
  return std::make_shared<ArrangementWorldData>(
      bw::core::arr::BuildArrangement({room}),
      wp::BoundingBox({-5.0f, -5.0f}, {20.0f, 20.0f}), 4.0f);
}

std::shared_ptr<ArrangementWorldData> makeCrossingStepWorld() {
  auto left = properties(0.0f, 30.0f, "left");
  left.floorZ.gradient = {0.0f, 1.0f};
  left.ceilingZ.gradient = {0.0f, 1.0f};
  auto right = properties(10.0f, 40.0f, "right");
  right.floorZ.gradient = {0.0f, -1.0f};
  right.ceilingZ.gradient = {0.0f, -1.0f};
  ArrangementPrimitive leftRoom{
      {rectangle(-1, 0, 0, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 0, 1, left};
  ArrangementPrimitive rightRoom{
      {rectangle(0, 0, 1, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 1, 2, right};
  return std::make_shared<ArrangementWorldData>(
      bw::core::arr::BuildArrangement({leftRoom, rightRoom}),
      wp::BoundingBox({-2.0f, -1.0f}, {4.0f, 12.0f}), 2.0f);
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

void triplanarSurfacesUseGenericAcousticsWithoutIdentifierInference() {
  auto triplanarProperties = properties(0.0f, 20.0f, "shared");
  // Deliberately collide with a Sub-material id. The explicit family tag must
  // win, while the ceiling and walls retain existing Sub-material resolution.
  triplanarProperties.floorMaterial =
      bw::core::SurfaceMaterialReference::triplanar("shared.floor");
  ArrangementPrimitive room{
      {rectangle(0, 0, 10, 10)}, Primitive::Operation::Union, Primitive::FillRule::EvenOdd, 0, 1, triplanarProperties};
  ArrangementWorldData world(
      bw::core::arr::BuildArrangement({room}),
      wp::BoundingBox({-5.0f, -5.0f}, {20.0f, 20.0f}), 4.0f);

  AcousticPreset generic{
      "builtin.acoustic.generic", "Generic", {0.1f, 0.2f, 0.3f}, 0.05f, {0.1f, 0.05f, 0.03f}};
  AcousticPreset collided{
      "test.collided", "Collided", {0.8f, 0.8f, 0.8f}, 0.8f, {0.8f, 0.8f, 0.8f}};
  AcousticMaterialResolver resolver =
      [&](auto const& material) -> AcousticPreset const& {
    using Material = std::remove_cvref_t<decltype(material)>;
    if constexpr (std::is_same_v<
                      Material, bw::core::SurfaceMaterialReference>) {
      return material.kind == bw::core::SurfaceMaterialKind::Triplanar
                 ? generic
                 : collided;
    } else {
      return collided;
    }
  };

  auto mesh = bw::app::ExportAcousticSceneMesh(world, resolver);
  require(std::ranges::any_of(mesh.materials, [&](auto const& material) {
            return material.id == generic.id;
          }),
          "Triplanar surface did not resolve to builtin.acoustic.generic");
  require(std::ranges::any_of(mesh.materials, [&](auto const& material) {
            return material.id == collided.id;
          }),
          "Sub-material surface acoustic resolution changed");
}

void exportHasEverySurfaceAndResolvedMaterial() {
  auto world = makeWorld();
  auto presets = makePresets();
  AcousticMaterialResolver resolver =
      [&](bw::core::SurfaceMaterialReference const& material)
      -> AcousticPreset const& { return presets.at(material.reference); };
  auto mesh = bw::app::ExportAcousticSceneMesh(*world, resolver);
  size_t wallTriangles = 0;
  size_t wallVertices = 0;
  for (auto const& wall : world->getWalls()) {
    if (!wall.visible) continue;
    auto surface = bw::core::arr::BuildArrangementWallSurface(
        world->getArrangement(), wall);
    wallTriangles += surface.vertexCount >= 3 ? surface.vertexCount - 2 : 0;
    wallVertices += surface.vertexCount;
  }
  require(mesh.triangles.size() ==
              world->getTriangles().size() * 2 + wallTriangles,
          "export did not contain every visible wall facet");
  require(mesh.vertices.size() ==
              world->getTriangles().size() * 6 + wallVertices,
          "exported walls did not retain their shared perimeter vertices");
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
    auto surface = bw::core::arr::BuildArrangementWallSurface(
        world->getArrangement(), wall);
    AcousticSceneVertex expected{
        orientation.normal.x, 0.0f, -orientation.normal.y};
    for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
      require(dot(triangleNormal(exportedWallTriangle), expected) > 0.0f,
              "exported wall winding does not match its canonical front side");
      ++exportedWallTriangle;
    }
  }
}

void exportUsesEvaluatedSurfaceGeometry() {
  auto world = makeSlopedWorld();
  auto presets = makePresets();
  for (auto surface : {std::string("floor"), std::string("ceiling"),
                       std::string("wall")}) {
    auto id = "slope." + surface;
    presets.emplace(
        id, AcousticPreset{id, id, {0.1f, 0.2f, 0.3f}, 0.05f, {0.01f, 0.02f, 0.03f}});
  }
  AcousticMaterialResolver resolver =
      [&](bw::core::SurfaceMaterialReference const& material)
      -> AcousticPreset const& { return presets.at(material.reference); };
  auto mesh = bw::app::ExportAcousticSceneMesh(*world, resolver);
  auto const& arrangement = world->getArrangement();

  size_t vertexOffset = 0;
  for (auto const& triangle : world->getTriangles()) {
    for (size_t corner = 0; corner < 3; ++corner) {
      require(std::abs(mesh.vertices[vertexOffset + corner].y -
                       triangle.floor.elevation[corner]) < 0.0001f,
              "Acoustic floor geometry flattened an evaluated elevation");
      require(std::abs(mesh.vertices[vertexOffset + 3 + corner].y -
                       triangle.ceiling.elevation[2 - corner]) < 0.0001f,
              "Acoustic ceiling geometry flattened an evaluated elevation");
    }
    auto floorNormal =
        cross(mesh.vertices[vertexOffset + 1] - mesh.vertices[vertexOffset],
              mesh.vertices[vertexOffset + 2] - mesh.vertices[vertexOffset]);
    auto ceilingNormal =
        cross(mesh.vertices[vertexOffset + 4] - mesh.vertices[vertexOffset + 3],
              mesh.vertices[vertexOffset + 5] - mesh.vertices[vertexOffset + 3]);
    AcousticSceneVertex expectedFloorNormal{
        triangle.floor.normal[0], triangle.floor.normal[2],
        -triangle.floor.normal[1]};
    AcousticSceneVertex expectedCeilingNormal{
        triangle.ceiling.normal[0], triangle.ceiling.normal[2],
        -triangle.ceiling.normal[1]};
    require(dot(floorNormal, expectedFloorNormal) > 0.0f &&
                dot(ceilingNormal, expectedCeilingNormal) > 0.0f,
            "Acoustic surface winding disagrees with generated normals");
    vertexOffset += 6;
  }

  for (auto const& wall : world->getWalls()) {
    if (!wall.visible) continue;
    auto surface =
        bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
    for (uint8_t corner = 0; corner < surface.vertexCount; ++corner) {
      auto const& vertex = surface.vertices[corner];
      AcousticSceneVertex expected{
          vertex.position.x, vertex.elevation, -vertex.position.y};
      auto delta = mesh.vertices[vertexOffset + corner] - expected;
      require(dot(delta, delta) < 0.000001f,
              "Acoustic wall geometry flattened an evaluated boundary");
    }
    vertexOffset += surface.vertexCount;
  }
  require(vertexOffset == mesh.vertices.size(),
          "Acoustic geometry validation did not consume the exported mesh");

  AcousticSceneVertex sample{4.0f, 10.0f, -5.0f};
  auto down = Ray{sample, {0.0f, -1.0f, 0.0f}};
  auto up = Ray{sample, {0.0f, 1.0f, 0.0f}};
  auto floorElevation =
      arrangement.palette[1].floorZ.evaluate({sample.x, -sample.z});
  auto ceilingElevation =
      arrangement.palette[1].ceilingZ.evaluate({sample.x, -sample.z});
  requireEquivalent(
      traceExportedMesh(mesh, down, 0.001f, 30.0f),
      Hit{sample.y - floorElevation, "slope.floor"},
      "sloped floor export");
  requireEquivalent(
      traceExportedMesh(mesh, up, 0.001f, 30.0f),
      Hit{ceilingElevation - sample.y, "slope.ceiling"},
      "sloped ceiling export");

  auto const& wall = world->getWalls().front();
  auto orientation = bw::core::arr::OrientArrangementWall(arrangement, wall);
  auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
  auto elevation =
      (orientation.bottomZ[0] + orientation.bottomZ[1] +
       orientation.topZ[0] + orientation.topZ[1]) *
      0.25f;
  AcousticSceneVertex wallNormal{
      orientation.normal.x, 0.0f, -orientation.normal.y};
  AcousticSceneVertex wallPoint{midpoint.x, elevation, -midpoint.y};
  auto wallRay = Ray{wallPoint + wallNormal * 2.0f, wallNormal * -1.0f};
  requireEquivalent(
      traceExportedMesh(mesh, wallRay, 0.001f, 4.0f),
      Hit{2.0f, "slope.wall"}, "variable-height wall export");
}

void crossingStepSegmentsExportAsOwnedFrontFacingTriangles() {
  auto world = makeCrossingStepWorld();
  auto presets = makePresets();
  AcousticMaterialResolver resolver =
      [&](bw::core::SurfaceMaterialReference const& material)
      -> AcousticPreset const& { return presets.at(material.reference); };
  auto mesh = bw::app::ExportAcousticSceneMesh(*world, resolver);
  auto const& arrangement = world->getArrangement();

  size_t triangularSteps = 0;
  for (auto const& wall : world->getWalls()) {
    auto const& edge = arrangement.edges[wall.edge];
    if (wall.kind == bw::core::arr::ArrangementWallKind::Border ||
        !arrangement.faces[edge.face[0]].solid ||
        !arrangement.faces[edge.face[1]].solid) {
      continue;
    }
    auto surface =
        bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
    require(surface.vertexCount == 3,
            "a crossing Step was not exported from a triangular surface");
    ++triangularSteps;
  }
  require(triangularSteps == 4,
          "the two crossing Step kinds did not produce four triangles");

  struct Candidate {
    float authoredY;
    float elevation;
    char const* material;
  };
  for (auto const& candidate : {
           Candidate{2.0f, 5.0f, "right.wall"},
           Candidate{8.0f, 5.0f, "left.wall"},
           Candidate{2.0f, 35.0f, "left.wall"},
           Candidate{8.0f, 35.0f, "right.wall"}}) {
    Ray ray{{-0.5f, candidate.elevation, -candidate.authoredY},
            {1.0f, 0.0f, 0.0f}};
    Hit expected{0.5f, candidate.material};
    requireEquivalent(
        traceArrangementReference(*world, resolver, ray, 0.001f, 2.0f),
        expected, "crossing Step reference");
    requireEquivalent(
        traceExportedMesh(mesh, ray, 0.001f, 2.0f), expected,
        "crossing Step export");
  }
}

void propertyRaysAgree() {
  auto world = makeWorld();
  auto presets = makePresets();
  AcousticMaterialResolver resolver =
      [&](bw::core::SurfaceMaterialReference const& material)
      -> AcousticPreset const& { return presets.at(material.reference); };
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
    triplanarSurfacesUseGenericAcousticsWithoutIdentifierInference();
    exportHasEverySurfaceAndResolvedMaterial();
    exportUsesEvaluatedSurfaceGeometry();
    crossingStepSegmentsExportAsOwnedFrontFacingTriangles();
    propertyRaysAgree();
    std::cout << "Acoustic scene export coverage passed\n";
  } catch (std::exception const& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
