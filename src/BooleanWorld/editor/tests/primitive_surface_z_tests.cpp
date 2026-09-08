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

void aGradientEditIsUndoable() {
  editor::clearUndoHistory();
  editor::Document document;
  document.newDoc();
  auto index = document.getWorld()->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f));
  auto* primitive = document.getWorld()->getPrimitive(index);

  auto changed = primitive->getProperties();
  changed.floorZ.gradient = {0.25f, -0.5f};
  changed.ceilingZ.gradient = {-0.125f, 0.75f};
  editor::transactUndoableAction(
      &document, "Edit Elevation gradients",
      [primitive, &changed](editor::Document* doc) {
        return editor::setPrimitiveProperties(doc, primitive, changed);
      });
  require(primitive->getProperties().floorZ.gradient == wp::Vector2{0.25f, -0.5f} &&
              primitive->getProperties().ceilingZ.gradient ==
                  wp::Vector2{-0.125f, 0.75f},
          "the gradient edit did not reach the Primitive");

  editor::undo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().floorZ.gradient ==
              wp::Vector2::ZERO,
          "undo did not restore the floor gradient");
  editor::redo(&document);
  require(document.getWorld()->getPrimitive(index)->getProperties().ceilingZ.gradient ==
              wp::Vector2{-0.125f, 0.75f},
          "redo did not restore the ceiling gradient");
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
    aGradientEditIsUndoable();
    theMoveIsUndoable();
    std::cout << "Primitive surface Z tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
