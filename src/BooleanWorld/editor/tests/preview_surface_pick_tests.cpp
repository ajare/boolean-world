#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/MeshPrimitive.h>

#include "PreviewSurfacePick.h"

namespace {

using bw::core::ClosedPolygon;
using bw::core::MeshFilledRegion;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using editor::PreviewSurface;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.01f;
}

ClosedPolygon square(float left, float bottom, float right, float top) {
  return {{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}};
}

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

std::unique_ptr<MeshPrimitive> makeRoom() {
  return makeRoomSpanning(-5, -5, 5, 5);
}

std::shared_ptr<bw::core::ArrangementWorldData> buildData(
    std::vector<Primitive*> primitives) {
  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), wp::BoundingBox{{-200, -200}, {400, 400}},
      16.0f, 1.0f);
}

void looksAtTheFloorWhenAimedDown() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {0, 0, -1});
  require(
      pick.hit() && pick.surfaceHit.surface == PreviewSurface::Floor,
      "aiming straight down did not pick the floor");
  require(near(pick.surfaceHit.distance, 10.0f), "the floor was not 10 units below");
  require(
      pick.primitiveIndex < data->getTriangles().size(),
      "the floor hit did not retain its Arrangement triangle index");
}

void looksAtTheCeilingWhenAimedUp() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {0, 0, 1});
  require(
      pick.hit() && pick.surfaceHit.surface == PreviewSurface::Ceiling,
      "aiming straight up did not pick the ceiling");
  require(near(pick.surfaceHit.distance, 10.0f), "the ceiling was not 10 units above");
}

void looksAtTheWallAheadRatherThanTheOneBehind() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {1, 0, 0});
  require(
      pick.hit() && pick.surfaceHit.surface == PreviewSurface::Wall,
      "aiming level did not pick a wall");
  require(near(pick.surfaceHit.distance, 5.0f), "the near wall was not 5 units ahead");
  require(
      pick.surfaceHit.wallIndex < data->getWalls().size(),
      "the wall hit did not retain its Arrangement wall index");
}

void picksTheNearestOfSeveralCandidates() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 2}, {1, 0, -1});
  require(
      pick.hit() && pick.surfaceHit.surface == PreviewSurface::Floor,
      "the nearer floor lost to a wall further along the ray");
}

void reportsNoHitWhenAimedAway() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  require(
      !editor::pickPreviewSceneSurface(*data, {100, 0, 10}, {1, 0, 0}).hit(),
      "a ray pointing away from the Arrangement reported a hit");
}

void reportsDistanceIndependentlyOfDirectionScale() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto unit = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {0, 0, -1});
  auto scaled = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {0, 0, -25});
  require(
      near(unit.surfaceHit.distance, scaled.surfaceHit.distance),
      "distance depended on the direction-vector length");
}

void ignoresDegenerateDirections() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  require(
      !editor::pickPreviewSceneSurface(*data, {0, 0, 10}, {0, 0, 0}).hit(),
      "a zero-length direction reported a hit");
}

// The old independent extrusions put duplicate walls at x=5. The resolved
// Arrangement removes that boolean seam entirely, so the ray reaches x=15.
void sharedBooleanSeamIsNotPickableAsAStaleWall() {
  auto left = makeRoomSpanning(-5, -5, 5, 5);
  auto right = makeRoomSpanning(5, -5, 15, 5);
  auto data = buildData({left.get(), right.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {1, 0, 0});
  require(
      pick.hit() && pick.surfaceHit.surface == PreviewSurface::Wall,
      "the composited room's outer wall was not hit");
  require(
      near(pick.surfaceHit.distance, 15.0f),
      "picking still saw stale per-Primitive geometry at the boolean seam");
}

void nearestResolvedWallWins() {
  auto nearRoom = makeRoomSpanning(-5, -5, 5, 5);
  auto farRoom = makeRoomSpanning(20, -5, 30, 5);
  auto data = buildData({nearRoom.get(), farRoom.get()});
  auto pick = editor::pickPreviewSceneSurface(
      *data, {0, 0, 10}, {1, 0, 0});
  require(
      pick.hit() && near(pick.surfaceHit.distance, 5.0f),
      "a distant Arrangement wall displaced the nearer one");
}

void emptyArrangementReportsNoHit() {
  auto data = buildData({});
  require(
      !editor::pickPreviewSceneSurface(*data, {0, 0, 10}, {1, 0, 0}).hit(),
      "an empty Arrangement reported a hit");
}

void surfacesAreNamedForDisplay() {
  require(
      editor::previewSurfaceName(PreviewSurface::Floor) == "Floor" &&
          editor::previewSurfaceName(PreviewSurface::Ceiling) == "Ceiling" &&
          editor::previewSurfaceName(PreviewSurface::Wall) == "Wall",
      "a surface was not named as the editor labels it");
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
    sharedBooleanSeamIsNotPickableAsAStaleWall();
    nearestResolvedWallWins();
    emptyArrangementReportsNoHit();
    surfacesAreNamedForDisplay();
    std::cout << "Preview surface pick tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
