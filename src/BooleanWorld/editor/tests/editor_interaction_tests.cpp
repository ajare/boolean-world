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

uint32_t addMeshWithHole(editor::Document& document) {
  auto* mesh = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {
          {
              {{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}},
              {{{-0.5f, -0.5f}}, {{-0.5f, 0.5f}}, {{0.5f, 0.5f}}, {{0.5f, -0.5f}}},
          },
      });
  mesh->setSize(20.0f, 20.0f);
  mesh->setPosition({0.0f, 0.0f});
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

void meshSubObjectClicksSupportModifiersAndRingCycling() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshVertexPickRadius = 0.25f;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  require(document.activateMesh(meshIndex), "could not activate test mesh");
  editor::EditorInteraction interaction;
  auto const* mesh = document.getActiveMesh();
  auto first = mesh->getFirstVertexIndex();
  auto second = mesh->getNextVertexIndex(first);

  auto click = pointerAt(mesh->getVertex(first).getPosition());
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getSelectedMeshVertexIndices() == std::set<uint32_t>{first},
          "plain mesh click did not replace the vertex selection");

  click = pointerAt(mesh->getVertex(second).getPosition());
  click.leftClicked = true;
  click.shift = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getSelectedMeshVertexIndices() ==
              std::set<uint32_t>({first, second}),
          "Shift-click did not add a mesh vertex");

  click.shift = false;
  click.control = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getSelectedMeshVertexIndices() == std::set<uint32_t>{first},
          "Ctrl-click did not toggle a mesh vertex");

  editor::setMeshSubMode(
      &document, settings, editor::Settings::MeshSubMode::Edge);
  settings.meshEdgeSelectionDistance = 0.25f;
  auto edge = mesh->getFirstEdgeIndex();
  click = pointerAt(mesh->getEdge(edge).getCentre());
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getSelectedMeshEdgeIndices() == std::set<uint32_t>{edge},
          "plain mesh click did not select an edge in Edge sub-mode");

  editor::setMeshSubMode(
      &document, settings, editor::Settings::MeshSubMode::Polygon);
  wp::Vector2 meshMin, meshMax;
  mesh->getExtents(meshMin, meshMax);
  auto meshCentre = (meshMin + meshMax) / 2.0f;
  click = pointerAt(meshCentre);
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  auto firstRing = *document.getSelectedMeshRingIndices().begin();
  interaction.updateSelection(&document, nullptr, settings, click);
  auto secondRing = *document.getSelectedMeshRingIndices().begin();
  require(firstRing != secondRing,
          "repeated clicks did not cycle through stacked Rings");
}

void meshRubberBandUsesContainmentAndModifierPolicies() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  document.activateMesh(meshIndex);

  wp::Vector2 meshMin, meshMax;
  document.getActiveMesh()->getExtents(meshMin, meshMax);
  auto meshCentre = (meshMin + meshMax) / 2.0f;

  settings.meshSubMode = editor::Settings::MeshSubMode::Edge;
  auto edges = document.getMeshSubObjectIndicesInBounds(
      wp::BoundingBox({meshMin.x - 1.0f, meshMin.y - 1.0f},
                      {meshMax.x - meshMin.x + 2.0f, 1.5f}),
      settings);
  require(edges.size() == 1,
          "edge rubber band did not require both endpoints to be contained");

  settings.meshSubMode = editor::Settings::MeshSubMode::Polygon;
  auto partialRings = document.getMeshSubObjectIndicesInBounds(
      wp::BoundingBox({meshMin.x - 1.0f, meshMin.y - 1.0f},
                      {meshMax.x - meshMin.x + 2.0f,
                       meshCentre.y - meshMin.y + 4.0f}),
      settings);
  require(partialRings.empty(),
          "Ring rubber band selected a Ring with vertices outside");
  auto containedRings = document.getMeshSubObjectIndicesInBounds(
      wp::BoundingBox(meshCentre - wp::Vector2{6.0f, 6.0f}, {12.0f, 12.0f}), settings);
  require(containedRings.size() == 1,
          "Ring rubber band did not select the wholly contained hole Ring");

  settings.meshSubMode = editor::Settings::MeshSubMode::Vertex;
  editor::EditorInteraction interaction;
  auto begin = pointerAt(meshMin - wp::Vector2{5.0f, 5.0f});
  begin.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, begin);
  auto drag = pointerAt(meshCentre);
  drag.leftDown = true;
  interaction.updateSelection(&document, nullptr, settings, drag);
  auto release = pointerAt(meshCentre);
  release.boxSelectStartWorld = meshMin - wp::Vector2{5.0f, 5.0f};
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  auto plainSelection = document.getSelectedMeshVertexIndices();
  require(!plainSelection.empty(), "plain mesh rubber band selected no vertices");

  begin.control = drag.control = release.control = true;
  interaction.updateSelection(&document, nullptr, settings, begin);
  interaction.updateSelection(&document, nullptr, settings, drag);
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedMeshVertexIndices().empty(),
          "Ctrl mesh rubber band did not toggle contained vertices");

  auto allVertices = document.getSelectableMeshSubObjectIndices(settings.meshSubMode);
  document.setSelectedMeshSubObjectIndices(
      settings.meshSubMode, {*allVertices.rbegin()});
  begin.control = drag.control = release.control = false;
  begin.shift = drag.shift = release.shift = true;
  interaction.updateSelection(&document, nullptr, settings, begin);
  interaction.updateSelection(&document, nullptr, settings, drag);
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedMeshVertexIndices().size() > plainSelection.size(),
          "Shift mesh rubber band did not add to the selection");
}

void meshSelectAllAndBoundsStayScopedToActiveMesh() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto first = addMesh(document, {0.0f, 0.0f});
  addMesh(document, {100.0f, 100.0f});
  document.activateMesh(first);

  editor::selectAllMeshSubObjects(
      &document, editor::Settings::MeshSubMode::Vertex);
  require(document.getSelectedMeshVertexIndices().size() == 4,
          "Select All escaped the active mesh");
  auto boxed = document.getMeshSubObjectIndicesInBounds(
      wp::BoundingBox({-1000.0f, -1000.0f}, {2000.0f, 2000.0f}), settings);
  require(boxed.size() == 4, "rubber band escaped the active mesh");

  document.clearActiveMesh();
  document.setSelectedPrimitiveIndices({first});
  editor::selectAllMeshSubObjects(
      &document, editor::Settings::MeshSubMode::Vertex);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{first},
          "mesh Select All changed selection with no active mesh");
}

void undoRestoresMeshSubObjectSelection() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {0});
  editor::transactUndoableAction(
      &document, "Change mesh selection",
      std::bind(editor::selectMeshSubObjects, std::placeholders::_1,
                editor::Settings::MeshSubMode::Vertex,
                std::set<uint32_t>{1}));
  editor::undo(&document);
  require(document.getActiveMeshPrimitiveIndex() == meshIndex &&
              document.getSelectedMeshVertexIndices() == std::set<uint32_t>{0},
          "undo did not restore active-mesh sub-object selection");
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
    meshSubObjectClicksSupportModifiersAndRingCycling();
    meshRubberBandUsesContainmentAndModifierPolicies();
    meshSelectAllAndBoundsStayScopedToActiveMesh();
    undoRestoresMeshSubObjectSelection();
    std::cout << "Editor selection interactions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
