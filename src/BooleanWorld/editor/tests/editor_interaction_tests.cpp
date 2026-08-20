#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/MeshPrimitive.h>
#include <core/RectanglePolygon.h>

#include "Actions.h"
#include "Defines.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}

void regenerateWorldData(Document*) {
}
}  // namespace editor

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

uint32_t addRectangle(
    editor::Document& document,
    wp::Vector2 const& position,
    float size = 10.0f) {
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setSize(size, size);
  primitive->setPosition(position);
  primitive->updateVertexPositions();
  document.getWorld()->addPrimitive(primitive);
  return primitive->getId();
}

uint32_t addMesh(editor::Document& document, wp::Vector2 const& position) {
  auto* mesh = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {{{{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}}}});
  mesh->setSize(10.0f, 10.0f);
  mesh->setPosition(position);
  mesh->updateVertexPositions();
  document.getWorld()->addPrimitive(mesh);
  return mesh->getId();
}

editor::PointerInput pointerAt(wp::Vector2 const& position) {
  editor::PointerInput input;
  input.screenPosition = position;
  input.worldPosition = position;
  input.cursorInWorldView = true;
  return input;
}

void plainControlAndShiftClicksApplyTheirSelectionPolicies() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto first = addRectangle(document, {50.0f, 50.0f});
  auto second = addRectangle(document, {100.0f, 100.0f});
  editor::EditorInteraction interaction;

  auto input = pointerAt({50.0f, 50.0f});
  input.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, input);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{first},
          "a plain click did not replace the Primitive selection");

  input = pointerAt({100.0f, 100.0f});
  input.leftClicked = true;
  input.shift = true;
  interaction.updateSelection(&document, nullptr, settings, input);
  require(document.getSelectedPrimitiveIndices() ==
              std::set<uint32_t>({first, second}),
          "a Shift-click did not add the Primitive to the selection");

  input.control = true;
  input.shift = false;
  interaction.updateSelection(&document, nullptr, settings, input);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{first},
          "a Ctrl-click did not toggle the Primitive out of the selection");
}

void repeatedClicksCycleThroughStackedPrimitives() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto first = addRectangle(document, {50.0f, 50.0f});
  auto second = addRectangle(document, {50.0f, 50.0f});
  editor::EditorInteraction interaction;

  auto press = pointerAt({50.0f, 50.0f});
  press.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, press);
  auto firstSelected = *document.getSelectedPrimitiveIndices().begin();
  require(firstSelected == first || firstSelected == second,
          "the first stacked click selected neither Primitive");

  auto release = pointerAt({50.0f, 50.0f});
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  auto secondSelected = *document.getSelectedPrimitiveIndices().begin();
  require(secondSelected != firstSelected,
          "the click cycle did not advance through stacked Primitives");

  interaction.updateSelection(&document, nullptr, settings, press);
  interaction.updateSelection(&document, nullptr, settings, release);
  require(*document.getSelectedPrimitiveIndices().begin() == firstSelected,
          "the click cycle did not wrap to the first stacked Primitive");
}

void modeAndSubModeChangesAreEditorPreferencesAndClearSelection() {
  editor::Document document;
  editor::Settings settings;
  document.newDoc();
  auto rectangle = addRectangle(document, {50.0f, 50.0f});
  auto mesh = document.getWorld()->convertPrimitivesToMesh({rectangle});
  auto nonMesh = addRectangle(document, {100.0f, 100.0f});
  document.setSelectedPrimitiveIndices({mesh});
  document.setModified(false);
  auto const undoLevels = editor::getUndoLevels();

  editor::setEditorMode(&document, settings, editor::Settings::Mode::Mesh);
  require(settings.mode == editor::Settings::Mode::Mesh,
          "switching to Mesh mode did not change the editor preference");
  require(settings.activeMeshPrimitiveIndex == mesh,
          "entering Mesh mode did not adopt the selected MeshPrimitive");
  require(!document.hasSelection(),
          "switching editor mode did not clear the selection");
  require(document.getHover({100.0f, 100.0f}, settings, nullptr).type ==
                  editor::HoverableType::None &&
              document.getSelectablePrimitiveIndices(settings) ==
                  std::vector<uint32_t>{mesh},
          "Mesh mode left a non-MeshPrimitive selectable");
  require(!document.isModified() && editor::getUndoLevels() == undoLevels,
          "switching editor mode dirtied the Document or entered undo history");

  document.setSelectedPrimitiveIndices({mesh});
  editor::setMeshSubMode(
      &document, settings, editor::Settings::MeshSubMode::Edge);
  require(settings.meshSubMode == editor::Settings::MeshSubMode::Edge &&
              !document.hasSelection(),
          "switching Mesh sub-mode did not clear the selection");
  require(!document.isModified() && editor::getUndoLevels() == undoLevels,
          "switching Mesh sub-mode dirtied the Document or entered undo history");

  editor::setEditorMode(&document, settings, editor::Settings::Mode::Primitive);
  require(settings.mode == editor::Settings::Mode::Primitive &&
              !document.hasSelection(),
          "leaving Mesh mode did not clear the selection");
}

void meshClicksBuildAndSwitchTheActiveProxy() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto first = addMesh(document, {50.0f, 50.0f});
  auto second = addMesh(document, {100.0f, 100.0f});
  editor::setEditorMode(&document, settings, editor::Settings::Mode::Mesh);
  editor::EditorInteraction interaction;

  auto click = pointerAt({50.0f, 50.0f});
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getActiveMeshPrimitiveIndex() == first && document.getActiveMesh(),
          "clicking an eligible MeshPrimitive did not build its proxy");

  click = pointerAt({100.0f, 100.0f});
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getActiveMeshPrimitiveIndex() == second &&
              settings.activeMeshPrimitiveIndex == second,
          "clicking another MeshPrimitive did not switch the active proxy");
}

void rubberBandSelectionSupportsPlainControlAndShiftPolicies() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto inside = addRectangle(document, {50.0f, 50.0f});
  auto outside = addRectangle(document, {500.0f, 500.0f});
  editor::EditorInteraction interaction;

  auto begin = pointerAt({0.0f, 0.0f});
  begin.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, begin);

  auto drag = pointerAt({100.0f, 100.0f});
  drag.leftDown = true;
  interaction.updateSelection(&document, nullptr, settings, drag);
  require(interaction.boxSelectDragging(),
          "an empty-background drag did not become a rubber-band selection");

  auto release = pointerAt({100.0f, 100.0f});
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{inside},
          "plain rubber-band selection did not replace the selection");

  // Ctrl toggles the hit, while Shift adds it to an existing selection.
  begin.control = true;
  interaction.updateSelection(&document, nullptr, settings, begin);
  drag.control = true;
  interaction.updateSelection(&document, nullptr, settings, drag);
  release.control = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedPrimitiveIndices().empty(),
          "Ctrl rubber-band selection did not toggle its hit");

  document.setSelectedPrimitiveIndices({outside});
  begin.control = false;
  begin.shift = true;
  drag.control = false;
  drag.shift = true;
  release.control = false;
  release.shift = true;
  interaction.updateSelection(&document, nullptr, settings, begin);
  interaction.updateSelection(&document, nullptr, settings, drag);
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedPrimitiveIndices() ==
              std::set<uint32_t>({inside, outside}),
          "Shift rubber-band selection did not add its hit");
}

}  // namespace

int main() {
  try {
    plainControlAndShiftClicksApplyTheirSelectionPolicies();
    repeatedClicksCycleThroughStackedPrimitives();
    modeAndSubModeChangesAreEditorPreferencesAndClearSelection();
    meshClicksBuildAndSwitchTheActiveProxy();
    rubberBandSelectionSupportsPlainControlAndShiftPolicies();
    std::cout << "Editor selection interactions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
