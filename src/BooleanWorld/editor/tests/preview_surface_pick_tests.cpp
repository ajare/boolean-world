#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/MeshPrimitive.h>

#include "PreviewSurfacePick.h"

namespace {

using bw::core::ClosedPolygon;
using bw::core::MeshFilledRegion;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using editor::PreviewSurface;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.01f;
}

ClosedPolygon square(float left, float bottom, float right, float top) {
  return {{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}};
}

// A 10x10 room spanning z (height) 0..20, so a viewer standing in the middle
// at eye height 10 is 10 units from the floor, the ceiling, and each wall.
std::unique_ptr<MeshPrimitive> makeRoom() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      std::vector<MeshFilledRegion>{{square(-5, -5, 5, 5), {}}}));
  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 20.0f;
  primitive->setProperties(properties);
  return primitive;
}

void looksAtTheFloorWhenAimedDown() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto hit = editor::pickPreviewSurface(geometry, {0, 0, 10}, {0, 0, -1});
  require(hit.hit(), "aiming straight down hit nothing");
  require(
      hit.surface == PreviewSurface::Floor,
      "aiming straight down did not pick the floor");
  require(near(hit.distance, 10.0f), "the floor was not 10 units below");
}

void looksAtTheCeilingWhenAimedUp() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto hit = editor::pickPreviewSurface(geometry, {0, 0, 10}, {0, 0, 1});
  require(
      hit.hit() && hit.surface == PreviewSurface::Ceiling,
      "aiming straight up did not pick the ceiling");
  require(near(hit.distance, 10.0f), "the ceiling was not 10 units above");
}

void looksAtTheWallAheadRatherThanTheOneBehind() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto hit = editor::pickPreviewSurface(geometry, {0, 0, 10}, {1, 0, 0});
  require(
      hit.hit() && hit.surface == PreviewSurface::Wall,
      "aiming level did not pick a wall");
  require(near(hit.distance, 5.0f), "the near wall was not 5 units ahead");

  // The wall the ray reaches first, not the far one it would also cross.
  auto const& quad = geometry.wallQuads[hit.wallIndex];
  for (auto const& vertex : quad.vertices) {
    require(near(vertex.x, 5.0f), "the wall behind the viewer was picked");
  }
}

void picksTheNearestOfSeveralCandidates() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  // Aimed down and forward: the floor is 2 units below, the wall 4 ahead.
  auto hit = editor::pickPreviewSurface(geometry, {0, 0, 2}, {1, 0, -1});
  require(
      hit.hit() && hit.surface == PreviewSurface::Floor,
      "the nearer floor lost to a wall further along the ray");
}

void reportsNoHitWhenAimedAway() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  // Outside the room, facing away from it.
  auto hit = editor::pickPreviewSurface(geometry, {100, 0, 10}, {1, 0, 0});
  require(!hit.hit(), "a ray pointing away from the Primitive reported a hit");
}

void reportsDistanceIndependentlyOfDirectionScale() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto unit = editor::pickPreviewSurface(geometry, {0, 0, 10}, {0, 0, -1});
  auto scaled = editor::pickPreviewSurface(geometry, {0, 0, 10}, {0, 0, -25});
  require(
      near(unit.distance, scaled.distance),
      "distance depended on the length of the direction vector");
}

void ignoresDegenerateDirections() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto hit = editor::pickPreviewSurface(geometry, {0, 0, 10}, {0, 0, 0});
  require(!hit.hit(), "a zero-length direction reported a hit");
}

}  // namespace

int main() {
  try {
    looksAtTheFloorWhenAimedDown();
    looksAtTheCeilingWhenAimedUp();
    looksAtTheWallAheadRatherThanTheOneBehind();
    picksTheNearestOfSeveralCandidates();
    reportsNoHitWhenAimedAway();
    reportsDistanceIndependentlyOfDirectionScale();
    ignoresDegenerateDirections();
    std::cout << "Preview surface pick tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
