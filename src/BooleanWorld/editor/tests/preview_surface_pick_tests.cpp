#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/MeshPrimitive.h>

#include "PreviewSurfaceOutline.h"
#include "PreviewSurfacePick.h"

namespace {

using bw::core::ClosedPolygon;
using bw::core::MeshFilledRegion;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using editor::PreviewSurface;
using Vector3 = std::array<float, 3>;

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

std::unique_ptr<MeshPrimitive> makeRoomSpanning(
    float left, float bottom, float right, float top,
    float floorZ, float ceilingZ, uint32_t id, uint8_t priority) {
  auto primitive = makeRoomSpanning(left, bottom, right, top);
  auto properties = primitive->getProperties();
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  properties.floorMaterialId = "floor." + std::to_string(id);
  properties.ceilingMaterialId = "ceiling." + std::to_string(id);
  properties.wallMaterialId = "wall." + std::to_string(id);
  properties.floorEmbossPresetId = "floor.emboss." + std::to_string(id);
  properties.ceilingEmbossPresetId = "ceiling.emboss." + std::to_string(id);
  properties.wallEmbossPresetId = "wall.emboss." + std::to_string(id);
  primitive->setProperties(properties);
  primitive->setPriority(priority);
  primitive->setId(id);
  return primitive;
}

// The editor resolves a picked surface to a Primitive exactly this way: the
// Arrangement names the owning Primitive's id, and the session looks that id
// up among the Primitives it drew.
Primitive* ownerPrimitive(
    bw::core::ArrangementWorldData const& data,
    editor::PreviewScenePick const& pick,
    std::vector<Primitive*> const& primitives) {
  auto owner = editor::resolvePreviewSurfaceOwner(data, pick);
  return owner.valid() && owner.primitiveListIndex < primitives.size()
             ? primitives[owner.primitiveListIndex]
             : nullptr;
}

void setSurfaceMaterial(
    Primitive* primitive, PreviewSurface surface, std::string const& id) {
  auto properties = primitive->getProperties();
  switch (surface) {
    case PreviewSurface::Floor:
      properties.floorMaterialId = id;
      break;
    case PreviewSurface::Ceiling:
      properties.ceilingMaterialId = id;
      break;
    default:
      properties.wallMaterialId = id;
      break;
  }
  primitive->setProperties(properties);
}

std::shared_ptr<bw::core::ArrangementWorldData> buildData(
    std::vector<Primitive*> primitives,
    bw::core::WedgeGenerationParameters const& wedgeSettings = {}) {
  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), wp::BoundingBox{{-200, -200}, {400, 400}},
      16.0f, nullptr, wedgeSettings);
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

void wedgesRemainAbsentFromSurfacePicking() {
  auto room = makeRoom();
  auto settings = bw::core::WedgeGenerationParameters{
      true, 4.0f, 4.0f, 2.0f, 2.0f, 2.0f, 2.0f};
  auto ordinary = buildData({room.get()});
  auto wedged = buildData({room.get()}, settings);
  auto origin = Vector3{0, 0, 10};
  auto direction = Vector3{1, 0, 0};
  auto before = editor::pickPreviewSceneSurface(*ordinary, origin, direction);
  auto after = editor::pickPreviewSceneSurface(*wedged, origin, direction);
  require(wedged->getDetail().getWedgeCount() == 16 &&
              before.surfaceHit.surface == after.surfaceHit.surface &&
              before.surfaceHit.wallIndex == after.surfaceHit.wallIndex &&
              near(before.surfaceHit.distance, after.surfaceHit.distance),
          "Wedge collision participation changed editor surface picking");
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

// Where two Primitives cover the same polygon, its properties come from the
// one that won the fold - so that is the Primitive whose floor material the
// polygon draws with, and the one an edit to that floor has to reach.
void overlappingFloorResolvesToTheWinningPrimitive() {
  auto lower = makeRoomSpanning(-5, -5, 5, 5, 0, 20, 1, 0);
  auto higher = makeRoomSpanning(0, -5, 10, 5, 0, 20, 2, 5);
  std::vector<Primitive*> primitives{lower.get(), higher.get()};
  auto data = buildData(primitives);

  auto overlap = editor::pickPreviewSceneSurface(*data, {2, 0, 10}, {0, 0, -1});
  require(
      ownerPrimitive(*data, overlap, primitives) == higher.get(),
      "the overlapped floor did not resolve to the higher-priority Primitive");
  require(
      editor::previewSurfaceSubMaterialId(*data, overlap) == "floor.2",
      "the overlapped floor did not report the winning Primitive's material");
  require(
      editor::previewSurfaceEmbossPresetId(*data, overlap) ==
          "floor.emboss.2",
      "the overlapped floor did not report its independent Emboss preset");

  auto outside = editor::pickPreviewSceneSurface(*data, {-3, 0, 10}, {0, 0, -1});
  require(
      ownerPrimitive(*data, outside, primitives) == lower.get() &&
          editor::previewSurfaceSubMaterialId(*data, outside) == "floor.1",
      "a floor outside the overlap did not resolve to its own Primitive");
}

// The whole point of resolving an owner: editing that Primitive's material
// for that surface has to be what the surface then draws with.
void editingTheResolvedOwnerChangesWhatTheSurfaceDraws() {
  auto lower = makeRoomSpanning(-5, -5, 5, 5, 0, 20, 1, 0);
  auto higher = makeRoomSpanning(0, -5, 10, 5, 0, 20, 2, 5);
  std::vector<Primitive*> primitives{lower.get(), higher.get()};
  auto data = buildData(primitives);

  for (auto const& [surface, origin, direction] :
       {std::tuple{PreviewSurface::Floor, Vector3{2, 0, 10}, Vector3{0, 0, -1}},
        std::tuple{PreviewSurface::Ceiling, Vector3{2, 0, 10}, Vector3{0, 0, 1}},
        std::tuple{PreviewSurface::Wall, Vector3{2, 0, 10}, Vector3{1, 0, 0}}}) {
    auto pick = editor::pickPreviewSceneSurface(*data, origin, direction);
    require(
        pick.hit() && pick.surfaceHit.surface == surface,
        "the surface under test was not the one picked");

    auto* owner = ownerPrimitive(*data, pick, primitives);
    require(owner != nullptr, "a picked surface resolved to no Primitive");
    setSurfaceMaterial(owner, surface, "picked.material");

    // The preview rebuilds its Arrangement snapshot after an assignment;
    // without that the palette still holds the material it was built with.
    auto rebuilt = buildData(primitives);
    auto again = editor::pickPreviewSceneSurface(*rebuilt, origin, direction);
    require(
        editor::previewSurfaceSubMaterialId(*rebuilt, again) ==
            "picked.material",
        "assigning the resolved owner's material did not change the surface");
    data = rebuilt;
  }

  require(
      higher->getProperties().floorMaterialId == "picked.material" &&
          higher->getProperties().ceilingMaterialId == "picked.material" &&
          higher->getProperties().wallMaterialId == "picked.material",
      "the three surfaces did not each resolve to the same overlapping owner");
  require(
      lower->getProperties().floorMaterialId == "floor.1",
      "editing one polygon's owner reached a Primitive it does not own");
}

// A wall belongs to the polygon that gave it its extent, not to whichever
// side of its edge comes first: the solid side of a border, the lower side of
// a floor step, the higher side of a ceiling step.
void wallsResolveToThePolygonTheyBound() {
  auto left = makeRoomSpanning(-5, -5, 5, 5, 0, 20, 1, 0);
  auto right = makeRoomSpanning(5, -5, 15, 5, 4, 10, 2, 0);
  std::vector<Primitive*> primitives{left.get(), right.get()};
  auto data = buildData(primitives);

  auto border = editor::pickPreviewSceneSurface(*data, {0, 0, 2}, {-1, 0, 0});
  require(
      border.surfaceHit.surface == PreviewSurface::Wall &&
          ownerPrimitive(*data, border, primitives) == left.get() &&
          editor::previewSurfaceSubMaterialId(*data, border) == "wall.1",
      "a border wall did not resolve to the solid polygon behind it");

  auto floorStep = editor::pickPreviewSceneSurface(*data, {0, 0, 2}, {1, 0, 0});
  require(
      floorStep.surfaceHit.surface == PreviewSurface::Wall &&
          ownerPrimitive(*data, floorStep, primitives) == left.get() &&
          editor::previewSurfaceSubMaterialId(*data, floorStep) == "wall.1",
      "a floor step did not resolve to the polygon its floor steps up from");

  auto ceilingStep =
      editor::pickPreviewSceneSurface(*data, {0, 0, 15}, {1, 0, 0});
  require(
      ceilingStep.surfaceHit.surface == PreviewSurface::Wall &&
          ownerPrimitive(*data, ceilingStep, primitives) == left.get() &&
          editor::previewSurfaceSubMaterialId(*data, ceilingStep) == "wall.1",
      "a ceiling step did not resolve to the polygon its ceiling steps down from");
}

// Primitive ids are Layer-local - Layer::_appendBuiltPrimitive stamps each
// one with its index in that Layer - so a World of several Layers holds a
// Primitive with id 0 per Layer, and resolving a surface by id picks whichever
// of them was looked up last. That was the bug behind "the selected surface
// keeps its old material": the assignment landed on a same-id Primitive in
// another Layer. Resolving by the Arrangement's own input order cannot be
// fooled that way.
void primitivesSharingAnIdAcrossLayersStillResolveApart() {
  auto lower = makeRoomSpanning(-5, -5, 5, 5, 0, 20, 0, 0);
  auto higher = makeRoomSpanning(0, -5, 10, 5, 0, 20, 0, 5);
  std::vector<Primitive*> primitives{lower.get(), higher.get()};
  auto data = buildData(primitives);

  auto overlap = editor::pickPreviewSceneSurface(*data, {2, 0, 10}, {0, 0, -1});
  auto outside = editor::pickPreviewSceneSurface(*data, {-3, 0, 10}, {0, 0, -1});
  require(
      ownerPrimitive(*data, overlap, primitives) == higher.get() &&
          ownerPrimitive(*data, outside, primitives) == lower.get(),
      "two Primitives sharing a Layer-local id resolved to the same owner");

  setSurfaceMaterial(higher.get(), PreviewSurface::Floor, "picked.material");
  auto rebuilt = buildData(primitives);
  require(
      editor::previewSurfaceSubMaterialId(
          *rebuilt,
          editor::pickPreviewSceneSurface(*rebuilt, {2, 0, 10}, {0, 0, -1})) ==
              "picked.material" &&
          editor::previewSurfaceSubMaterialId(
              *rebuilt, editor::pickPreviewSceneSurface(
                            *rebuilt, {-3, 0, 10}, {0, 0, -1})) == "floor.0",
      "the assignment reached the wrong Primitive of the two sharing an id");
}

// Shift+Up/Down in the preview moves the selected floor or ceiling, and it
// resolves its Primitive exactly as a material assignment does - so the
// polygon under the cursor is the one that rises, and its neighbours stay
// where they are.
void movingTheResolvedOwnerRaisesOnlyThatPolygon() {
  auto lower = makeRoomSpanning(-5, -5, 5, 5, 0, 20, 1, 0);
  auto higher = makeRoomSpanning(0, -5, 10, 5, 0, 20, 2, 5);
  std::vector<Primitive*> primitives{lower.get(), higher.get()};
  auto data = buildData(primitives);

  auto pick = editor::pickPreviewSceneSurface(*data, {2, 0, 20}, {0, 0, -1});
  auto* owner = ownerPrimitive(*data, pick, primitives);
  require(owner == higher.get(), "the floor under the cursor resolved elsewhere");

  auto properties = owner->getProperties();
  properties.floorZ += 8.0f;
  owner->setProperties(properties);
  auto rebuilt = buildData(primitives);

  auto raised = editor::pickPreviewSceneSurface(*rebuilt, {2, 0, 20}, {0, 0, -1});
  require(
      raised.surfaceHit.surface == PreviewSurface::Floor &&
          near(raised.surfaceHit.distance, 12.0f),
      "the selected floor did not rise by the eight units it was nudged");

  auto neighbour =
      editor::pickPreviewSceneSurface(*rebuilt, {-3, 0, 20}, {0, 0, -1});
  require(
      neighbour.surfaceHit.surface == PreviewSurface::Floor &&
          near(neighbour.surfaceHit.distance, 20.0f),
      "moving one polygon's floor moved a polygon it does not own");

  // The step it just made is a wall, and that wall belongs to the polygon
  // whose floor stayed low - the one you would be standing on to see it.
  auto step = editor::pickPreviewSceneSurface(*rebuilt, {-3, 0, 4}, {1, 0, 0});
  require(
      step.surfaceHit.surface == PreviewSurface::Wall &&
          ownerPrimitive(*rebuilt, step, primitives) == lower.get(),
      "the step the nudge created did not resolve to the lower polygon");
}

void outlinesUseTheRenderersReflectedGroundPlane() {
  auto room = makeRoomSpanning(10.0f, 30.0f, 20.0f, 40.0f);
  auto data = buildData({room.get()});
  auto floor = editor::pickPreviewSceneSurface(
      *data, {15.0f, 35.0f, 10.0f}, {0.0f, 0.0f, -1.0f});
  auto floorOutline = editor::previewSurfaceOutline(*data, floor);
  require(!floorOutline.empty(), "the asymmetric floor produced no outline");
  for (auto const& point : floorOutline) {
    require(
        point[0] >= 9.99f && point[0] <= 20.01f && near(point[1], 0.0f) &&
            point[2] <= -29.99f && point[2] >= -40.01f,
        "the floor outline did not map Arrangement (x,y,z) to renderer (x,z,-y)");
  }

  auto wall = editor::pickPreviewSceneSurface(
      *data, {5.0f, 35.0f, 10.0f}, {1.0f, 0.0f, 0.0f});
  auto wallOutline = editor::previewSurfaceOutline(*data, wall);
  require(
      wall.surfaceHit.surface == PreviewSurface::Wall && wallOutline.size() == 8,
      "the hovered wall did not produce its complete four-edge outline");
  for (auto const& point : wallOutline) {
    require(
        near(point[0], 10.0f) && point[1] >= -0.01f && point[1] <= 20.01f &&
            point[2] <= -29.99f && point[2] >= -40.01f,
        "the wall outline did not coincide with WorldRenderer wall geometry");
  }
}

void unpickedSurfacesResolveToNothing() {
  auto room = makeRoom();
  auto data = buildData({room.get()});
  auto miss = editor::pickPreviewSceneSurface(*data, {100, 0, 10}, {1, 0, 0});
  require(
      !editor::resolvePreviewSurfaceOwner(*data, miss).valid() &&
          editor::previewSurfaceSubMaterialId(*data, miss).empty(),
      "a ray that hit nothing still resolved to an owning Primitive");
}

}  // namespace

int main() {
  try {
    looksAtTheFloorWhenAimedDown();
    looksAtTheCeilingWhenAimedUp();
    looksAtTheWallAheadRatherThanTheOneBehind();
    picksTheNearestOfSeveralCandidates();
    wedgesRemainAbsentFromSurfacePicking();
    reportsNoHitWhenAimedAway();
    reportsDistanceIndependentlyOfDirectionScale();
    ignoresDegenerateDirections();
    sharedBooleanSeamIsNotPickableAsAStaleWall();
    nearestResolvedWallWins();
    emptyArrangementReportsNoHit();
    surfacesAreNamedForDisplay();
    overlappingFloorResolvesToTheWinningPrimitive();
    editingTheResolvedOwnerChangesWhatTheSurfaceDraws();
    wallsResolveToThePolygonTheyBound();
    primitivesSharingAnIdAcrossLayersStillResolveApart();
    movingTheResolvedOwnerRaisesOnlyThatPolygon();
    outlinesUseTheRenderersReflectedGroundPlane();
    unpickedSurfacesResolveToNothing();
    std::cout << "Preview surface pick tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
