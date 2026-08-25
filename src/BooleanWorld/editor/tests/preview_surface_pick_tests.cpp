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

// A room occupying an arbitrary span, so two of them can be butted together
// to share a wall plane exactly as neighbouring Primitives do.
std::unique_ptr<MeshPrimitive> makeRoomSpanning(
    float left, float bottom, float right, float top) {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      std::vector<MeshFilledRegion>{{square(left, bottom, right, top), {}}}));
  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 20.0f;
  primitive->setProperties(properties);
  return primitive;
}

// Primitives are extruded independently, so neighbours sharing an edge each
// loft their own wall at that plane. The renderer draws them in order with
// GL_LEQUAL, so the later one owns those pixels - and picking the earlier one
// would mark a surface hidden behind its own duplicate.
void coincidentWallsResolveToTheOneDrawnLast() {
  auto left = makeRoomSpanning(-5, -5, 5, 5);
  auto right = makeRoomSpanning(5, -5, 15, 5);
  auto leftGeometry = editor::extrudePrimitiveForPreview(*left);
  auto rightGeometry = editor::extrudePrimitiveForPreview(*right);

  std::vector<editor::PrimitivePreviewGeometry const*> scene{
      &leftGeometry, &rightGeometry};
  auto pick = editor::pickPreviewSceneSurface(scene, {0, 0, 10}, {1, 0, 0});

  require(pick.hit(), "the shared wall was not hit at all");
  require(
      pick.surfaceHit.surface == PreviewSurface::Wall,
      "the shared wall was not picked as a wall");
  require(
      pick.primitiveIndex == 1,
      "a wall coincident with a later Primitive's did not defer to it");
}

void theNearestPrimitiveStillWinsWhenNotCoincident() {
  auto nearRoom = makeRoomSpanning(-5, -5, 5, 5);
  auto farRoom = makeRoomSpanning(20, -5, 30, 5);
  auto nearGeometry = editor::extrudePrimitiveForPreview(*nearRoom);
  auto farGeometry = editor::extrudePrimitiveForPreview(*farRoom);

  // The further Primitive is drawn last, so it must not win on order alone.
  std::vector<editor::PrimitivePreviewGeometry const*> scene{
      &nearGeometry, &farGeometry};
  auto pick = editor::pickPreviewSceneSurface(scene, {0, 0, 10}, {1, 0, 0});

  require(pick.hit(), "nothing was hit along the ray");
  require(
      pick.primitiveIndex == 0,
      "a distant Primitive drawn later displaced the nearer one");
  require(
      near(pick.surfaceHit.distance, 5.0f),
      "the reported distance was not that of the nearest wall");
}

void surfacesAreNamedForDisplay() {
  require(
      editor::previewSurfaceName(PreviewSurface::Floor) == "Floor" &&
          editor::previewSurfaceName(PreviewSurface::Ceiling) == "Ceiling" &&
          editor::previewSurfaceName(PreviewSurface::Wall) == "Wall",
      "a surface was not named as the editor labels it");
}

void anEmptySceneReportsNoHit() {
  std::vector<editor::PrimitivePreviewGeometry const*> scene;
  require(
      !editor::pickPreviewSceneSurface(scene, {0, 0, 10}, {1, 0, 0}).hit(),
      "an empty scene reported a hit");
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
    coincidentWallsResolveToTheOneDrawnLast();
    theNearestPrimitiveStillWinsWhenNotCoincident();
    anEmptySceneReportsNoHit();
    surfacesAreNamedForDisplay();
    std::cout << "Preview surface pick tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
