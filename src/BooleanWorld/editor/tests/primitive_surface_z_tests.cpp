#include <iostream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/LayerBuildStep.h>
#include <core/RectanglePolygon.h>

#include "Actions.h"
#include "Document.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = nullptr;
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}
void regenerateWorldData(Document*) {
}
}  // namespace editor

namespace {

using editor::PrimitiveMaterialSurface;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bw::core::PrimitivePropertySet room(float floorZ, float ceilingZ) {
  bw::core::PrimitivePropertySet properties;
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  return properties;
}

// Shift+Up/Down in the 3D preview moves the selected floor or ceiling by
// eight units, and Ctrl+Shift by one - both of them through this.
void aNudgeMovesOneSurfaceByItsOwnStep() {
  auto const original = room(0.0f, 48.0f);

  auto floorUp = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Floor, 8.0f);
  require(
      floorUp.floorZ == 8.0f && floorUp.ceilingZ == original.ceilingZ,
      "raising the floor did not move the floor alone");

  auto ceilingDown = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Ceiling, -1.0f);
  require(
      ceilingDown.ceilingZ == 47.0f && ceilingDown.floorZ == original.floorZ,
      "lowering the ceiling did not move the ceiling alone");

  auto floorDown = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Floor, -8.0f);
  require(floorDown.floorZ == -8.0f, "a floor could not be moved below zero");
}

// The two surfaces are the top and bottom of one room: letting either pass
// the other turns it inside out and gives its walls a negative height.
void neitherSurfacePassesTheOther() {
  auto const original = room(0.0f, 4.0f);

  auto floorUp = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Floor, 8.0f);
  require(
      floorUp.floorZ == original.ceilingZ,
      "a floor nudged past its ceiling was not stopped at it");

  auto ceilingDown = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Ceiling, -8.0f);
  require(
      ceilingDown.ceilingZ == original.floorZ,
      "a ceiling nudged past its floor was not stopped at it");

  // Already flat: the next nudge in the same direction changes nothing at
  // all, which is how the preview knows not to open a transaction for it.
  auto const flat = room(4.0f, 4.0f);
  auto again = editor::movedSurfaceZ(
      flat, PrimitiveMaterialSurface::Floor, 8.0f);
  require(
      again.floorZ == flat.floorZ && again.ceilingZ == flat.ceilingZ,
      "a nudge with nowhere left to go still reported a change");
}

// A wall is the gap between two polygons, not a surface with a height of its
// own - so the preview nudges nothing when one is selected.
void wallsHaveNoHeightToMove() {
  auto const original = room(0.0f, 48.0f);
  auto moved = editor::movedSurfaceZ(
      original, PrimitiveMaterialSurface::Wall, 8.0f);
  require(
      moved.floorZ == original.floorZ && moved.ceilingZ == original.ceilingZ,
      "nudging a wall moved the Primitive's floor or ceiling");
}

void aWaterLevelNudgeOnlyChangesSelectedFloors() {
  auto properties = room(0.0f, 48.0f);
  properties.liquidLevel = 4.0f;

  auto raised = editor::movedLiquidLevel(
      properties, PrimitiveMaterialSurface::Floor, 8.0f);
  require(
      raised.liquidLevel == 12.0f && raised.floorZ == properties.floorZ &&
          raised.ceilingZ == properties.ceilingZ,
      "raising the selected floor's water changed the wrong property");

  auto lowered = editor::movedLiquidLevel(
      properties, PrimitiveMaterialSurface::Floor, -8.0f);
  require(
      lowered.liquidLevel == 0.0f,
      "lowering water allowed its authored level to become negative");

  auto ceiling = editor::movedLiquidLevel(
      properties, PrimitiveMaterialSurface::Ceiling, 8.0f);
  require(
      ceiling.liquidLevel == properties.liquidLevel,
      "a water-level nudge changed a selected ceiling");
}

void elevationSpansFitThePrimitiveAtTheirAuthoredAngle() {
  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  primitive.setSize(2.0f, 4.0f);

  auto properties = primitive.getProperties();
  properties.floorSpan = {0.0f, 10.0f, 30.0f};
  properties.ceilingSpan = {90.0f, 40.0f, 80.0f};
  primitive.setProperties(properties);

  auto floor = primitive.getElevationPlane(
      bw::core::PrimitiveSurface::Floor);
  require(floor.evaluate({0.0f, -2.0f}) == 10.0f &&
              floor.evaluate({0.0f, 2.0f}) == 30.0f,
          "zero degrees did not interpolate from lower to upper along local +Y");

  auto ceiling = primitive.getElevationPlane(
      bw::core::PrimitiveSurface::Ceiling);
  require(ceiling.evaluate({1.0f, 0.0f}) == 40.0f &&
              ceiling.evaluate({-1.0f, 0.0f}) == 80.0f,
          "90 degrees counter-clockwise from +Y did not interpolate toward -X");

  auto bounds = primitive.getElevationBounds(0.0f);
  require(bounds.corners[0] == wp::Vector2{-1.0f, -2.0f} &&
              bounds.corners[2] == wp::Vector2{1.0f, 2.0f},
          "the slope OBB did not fit the Primitive at the authored angle");

  primitive.setSize(2.0f, 8.0f);
  floor = primitive.getElevationPlane(bw::core::PrimitiveSurface::Floor);
  require(floor.evaluate({0.0f, -4.0f}) == 10.0f &&
              floor.evaluate({0.0f, 4.0f}) == 30.0f,
          "editing Primitive geometry did not refit its Elevation span");

  primitive.setSize(2.0f, 0.0f);
  floor = primitive.getElevationPlane(bw::core::PrimitiveSurface::Floor);
  require(floor.gradient == wp::Vector2::ZERO &&
              floor.evaluate({0.0f, 100.0f}) == 10.0f,
          "a zero-length Elevation span did not use its lower elevation");
}

void theViewFacingElevationSpanEndCanBeNudgedIndependently() {
  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  primitive.setSize(4.0f, 8.0f);
  auto properties = primitive.getProperties();
  properties.floorSpan = {0.0f, 0.0f, 8.0f};
  properties.ceilingSpan = {90.0f, 12.0f, 12.0f};
  primitive.setProperties(properties);

  auto lookingNorth = editor::elevationSpanEndTowardsView(
      primitive, PrimitiveMaterialSurface::Floor, {0.0f, 1.0f});
  auto lookingSouth = editor::elevationSpanEndTowardsView(
      primitive, PrimitiveMaterialSurface::Floor, {0.0f, -1.0f});
  auto ceilingLookingWest = editor::elevationSpanEndTowardsView(
      primitive, PrimitiveMaterialSurface::Ceiling, {-1.0f, 0.0f});
  auto lookingAcross = editor::elevationSpanEndTowardsView(
      primitive, PrimitiveMaterialSurface::Floor, {1.0f, 0.0f});
  require(
      lookingNorth == editor::ElevationSpanEnd::Upper &&
          lookingSouth == editor::ElevationSpanEnd::Lower &&
          ceilingLookingWest == editor::ElevationSpanEnd::Upper &&
          !lookingAcross,
      "the Elevation span end ahead of the view was not selected");

  // Position is irrelevant; orientation rotates which World direction leads
  // toward each authored end.
  primitive.setPosition({10.0f, 5.0f});
  primitive.setOrientation(90.0f);
  require(
      editor::elevationSpanEndTowardsView(
          primitive, PrimitiveMaterialSurface::Floor, {1.0f, 0.0f}) ==
          editor::ElevationSpanEnd::Upper,
      "the view-facing Elevation span end did not follow Primitive rotation");

  auto raised = editor::movedElevationSpanEnd(
      primitive, PrimitiveMaterialSurface::Floor,
      editor::ElevationSpanEnd::Upper, 2.0f);
  require(
      raised.floorSpan.lowerElevation == 0.0f &&
          raised.floorSpan.upperElevation == 10.0f &&
          raised.ceilingSpan == properties.ceilingSpan,
      "raising a slope end changed more than the selected floor end");

  auto clamped = editor::movedElevationSpanEnd(
      primitive, PrimitiveMaterialSurface::Floor,
      editor::ElevationSpanEnd::Upper, 8.0f);
  require(
      clamped.floorSpan.upperElevation == 12.0f,
      "a raised floor slope end passed through its ceiling");

  auto loweredCeiling = editor::movedElevationSpanEnd(
      primitive, PrimitiveMaterialSurface::Ceiling,
      editor::ElevationSpanEnd::Lower, -20.0f);
  require(
      loweredCeiling.ceilingSpan.lowerElevation == 8.0f &&
          loweredCeiling.ceilingSpan.upperElevation == 12.0f,
      "a lowered ceiling slope end passed through its floor");
}

void anElevationSpanEditIsUndoable() {
  editor::clearUndoHistory();
  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = document.getWorld()->getPrimitive(index);

  auto changed = primitive->getProperties();
  changed.floorSpan = {0.0f, -8.0f, 16.0f};
  changed.ceilingSpan = {90.0f, 40.0f, 72.0f};
  editor::transactUndoableAction(
      &document, "Edit Elevation spans",
      [primitive, &changed](editor::Document* doc) {
        return editor::setPrimitiveProperties(doc, primitive, changed);
      });
  require(primitive->getProperties().floorSpan == changed.floorSpan &&
              primitive->getProperties().ceilingSpan == changed.ceilingSpan,
          "the Elevation-span edit did not reach the Primitive");

  editor::undo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().floorSpan ==
              bw::core::ElevationSpan{0.0f, 0.0f, 0.0f},
          "undo did not restore the floor Elevation span");
  editor::redo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().ceilingSpan ==
              changed.ceilingSpan,
          "redo did not restore the ceiling Elevation span");
}

void theMoveIsUndoable() {
  editor::clearUndoHistory();
  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = document.getWorld()->getPrimitive(index);

  auto const before = primitive->getProperties().floorZ;
  auto const moved = editor::movedSurfaceZ(
      primitive->getProperties(), PrimitiveMaterialSurface::Floor, 8.0f);
  editor::transactUndoableAction(
      &document, "Move preview surface", [primitive, &moved](editor::Document* doc) {
        return editor::setPrimitiveProperties(doc, primitive, moved);
      });
  require(
      document.getWorld()->getPrimitive(index)->getProperties().floorZ ==
          before + 8.0f,
      "the surface move did not reach the Primitive");

  editor::undo(&document);
  require(
      document.getWorld()->getPrimitive(index)->getProperties().floorZ == before,
      "undo did not restore the floor to where it was");
  editor::redo(&document);
  require(
      document.getWorld()->getPrimitive(index)->getProperties().floorZ ==
          before + 8.0f,
      "redo did not move the floor back");
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    aNudgeMovesOneSurfaceByItsOwnStep();
    neitherSurfacePassesTheOther();
    wallsHaveNoHeightToMove();
    aWaterLevelNudgeOnlyChangesSelectedFloors();
    elevationSpansFitThePrimitiveAtTheirAuthoredAngle();
    theViewFacingElevationSpanEndCanBeNudgedIndependently();
    anElevationSpanEditIsUndoable();
    theMoveIsUndoable();
    std::cout << "Primitive surface Z tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
