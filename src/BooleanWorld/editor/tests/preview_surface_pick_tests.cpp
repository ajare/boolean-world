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

void eachSurfaceReportsItsOwnMaterial() {
  auto room = makeRoom();
  auto properties = room->getProperties();
  // Distinct indices, so a surface reading the wrong one is visible here
  // rather than hiding behind a shared default.
  properties.floorMaterialIndex = 0;
  properties.ceilingMaterialIndex = 1;
  properties.wallMaterialIndex = 1;
  room->setProperties(properties);
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto const* floor =
      editor::previewSurfaceMaterial(geometry, PreviewSurface::Floor);
  auto const* ceiling =
      editor::previewSurfaceMaterial(geometry, PreviewSurface::Ceiling);
  auto const* wall =
      editor::previewSurfaceMaterial(geometry, PreviewSurface::Wall);

  require(floor && floor->index == 0, "the floor reported another material");
  require(
      ceiling && ceiling->index == 1, "the ceiling reported another material");
  require(wall && wall->index == 1, "the wall reported another material");
  require(
      editor::previewSurfaceMaterial(geometry, PreviewSurface::None) == nullptr,
      "an unpicked surface still reported a material");
}

void materialsAreNamedFromTheRegistry() {
  require(
      editor::previewMaterialName(0) == "Marble",
      "material 0 was not named Marble");
  require(
      editor::previewMaterialName(1) == "Stone",
      "material 1 was not named Stone");
  // A Primitive can carry an index the build no longer knows about.
  require(
      editor::previewMaterialName(9999) == "Unknown",
      "an out-of-range material index did not answer Unknown");
}

void materialParametersStopAtTheDeclaredCount() {
  // Stone declares 3 parameters, but the registry sizes every material's
  // table at BW_MATERIAL_PARAMS_MAX and leaves the rest zeroed. Reading the
  // whole table would offer nameless controls over slots the shader ignores.
  auto stone = editor::previewMaterialParameters(1);
  require(stone.size() == 3, "Stone did not report exactly its 3 parameters");
  require(
      stone[0].name == "base_scale" && stone[1].name == "medium_scale" &&
          stone[2].name == "stone_mix",
      "Stone's parameters were not the ones the registry lists");

  auto marble = editor::previewMaterialParameters(0);
  require(marble.size() == 8, "Marble did not report all 8 parameters");
  require(
      marble[0].name == "warp_scale" && near(marble[0].minimum, 0.0f) &&
          near(marble[0].maximum, 5.0f) &&
          near(marble[0].defaultValue, 1.35f),
      "Marble's first parameter did not match the registry");
}

void materialParametersAddressTheirOwnShaderSlot() {
  for (uint32_t materialIndex = 0; materialIndex < 2; ++materialIndex) {
    auto parameters = editor::previewMaterialParameters(materialIndex);
    for (size_t i = 0; i < parameters.size(); ++i) {
      require(
          parameters[i].index == (uint32_t)i,
          "a parameter did not address the slot it occupies");
      require(
          parameters[i].maximum > parameters[i].minimum,
          "a parameter offered a range that cannot be moved through");
      require(
          !parameters[i].name.empty(),
          "a parameter was offered without a name to label it");
    }
  }
}

void unknownMaterialsHaveNoParameters() {
  require(
      editor::previewMaterialParameters(9999).empty(),
      "an out-of-range material offered parameters");
}

void aSurfaceMaterialCanBeEditedInPlace() {
  auto room = makeRoom();
  auto geometry = editor::extrudePrimitiveForPreview(*room);

  auto* wall = editor::previewSurfaceMaterial(geometry, PreviewSurface::Wall);
  require(wall != nullptr, "the wall material could not be addressed");
  wall->definition.params[0] = 4.25f;

  // The renderer reads the geometry, so an edit has to land there to show up.
  require(
      near(geometry.wallMaterial.definition.params[0], 4.25f),
      "editing through the mutable lookup did not reach the geometry");
  require(
      near(geometry.floorMaterial.definition.params[0], 0.0f),
      "editing the wall disturbed another surface's material");
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
    eachSurfaceReportsItsOwnMaterial();
    materialsAreNamedFromTheRegistry();
    surfacesAreNamedForDisplay();
    materialParametersStopAtTheDeclaredCount();
    materialParametersAddressTheirOwnShaderSlot();
    unknownMaterialsHaveNoParameters();
    aSurfaceMaterialCanBeEditedInPlace();
    std::cout << "Preview surface pick tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
