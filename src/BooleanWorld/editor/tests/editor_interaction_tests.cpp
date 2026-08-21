#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <core/LayerBuildStep.h>
#include <core/DefinePrefabs.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/PrefabField.h>
#include <core/MeshPrimitive.h>
#include <core/PrimitiveField.h>
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

class CountingWorldDataGenerator final : public bw::core::WorldDataGenerator {
public:
  uint32_t generationRequests{0};

  bw::core::WorldDataGenerator* copy() override {
    return new CountingWorldDataGenerator(*this);
  }

  bw::core::WorldDataPtr getWorldData(bw::core::World const*) override {
    return nullptr;
  }

  void generate(bw::core::World const*, bool) override {
    ++generationRequests;
  }
};

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

uint32_t addPolygonMesh(
    editor::Document& document,
    wp::Vector2 const& position,
    std::vector<wp::Vector2> const& points) {
  bw::core::ClosedPolygon ring(points.begin(), points.end());
  auto* mesh = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {{ring}});
  mesh->setSize(10.0f, 10.0f);
  mesh->setPosition(position);
  mesh->updateVertexPositions();
  document.getWorld()->addPrimitive(mesh);
  return mesh->getId();
}

// A step that produces nothing and refuses everything, for the draw tool's
// "the selected step accepts new Primitives" arming rule.
class RefusingStep final : public bw::core::LayerBuildStep {
public:
  std::string getType() const override {
    return "RefusingStep";
  }

  bool mayBeFirstStep() const override {
    return false;
  }

  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*, bw::core::VertexTransformerObject*>&) const override {
    return new RefusingStep();
  }

  void execute(bw::core::LayerBuildContext&) const override {
  }

  bool primitivesParticipateInBuild() const override {
    return true;
  }

  bool permitsDirectPrimitiveEditing() const override {
    return false;
  }

  bool acceptsNewPrimitives() const override {
    return false;
  }

  uint32_t adoptPrimitive(bw::core::Primitive*) override {
    return 0;
  }

  void replacePrimitive(bw::core::Primitive*, bw::core::Primitive*) override {
  }

  bool ownsPrimitive(bw::core::Primitive const*) const override {
    return false;
  }

private:
  void serializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return true;
  }
};

float twiceSignedArea(std::vector<wp::Vector2> const& points) {
  float area = 0.0f;
  for (size_t i = 0; i < points.size(); ++i) {
    auto const& a = points[i];
    auto const& b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area;
}

// The active mesh's only Ring, in its stored order, for winding checks.
std::vector<wp::Vector2> activeRingPositions(editor::Document const& document) {
  std::vector<wp::Vector2> positions;
  auto const* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  for (auto vertexIndex : mesh->getPolygon(ringIndex).getOrderedVertexIndices()) {
    positions.push_back(mesh->getVertex(vertexIndex).getPosition());
  }
  return positions;
}

editor::Settings meshDrawSettings() {
  editor::Settings settings;
  settings.ghostActive = false;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Vertex;
  return settings;
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

void deletePrimitivesRefusesTheGhostEvenWhenHandedItsIndexDirectly() {
  editor::Document document;
  document.newDoc();
  auto primitiveIndex = addRectangle(document, {50.0f, 50.0f});

  editor::deletePrimitives(&document, {uint32_t(ED_GHOST_INDEX), primitiveIndex});

  require(document.getGhost() != nullptr,
          "deletePrimitives destroyed the ghost when handed its index directly");
  require(document.getWorld()->getNumPrimitives() == 1,
          "deletePrimitives did not delete the real Primitive alongside the refused ghost");
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
  auto release = pointerAt(meshCentre);
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  auto secondRing = *document.getSelectedMeshRingIndices().begin();
  require(firstRing != secondRing,
          "repeated clicks did not cycle through stacked Rings");

  uint32_t holeRing = ~0u;
  for (auto polygon = mesh->getFirstPolygonIndex();
       !mesh->polygonIndexIterationFinished(polygon);
       polygon = mesh->getNextPolygonIndex(polygon)) {
    if (mesh->getPolygon(polygon).isHole()) {
      holeRing = polygon;
      break;
    }
  }
  require(holeRing != ~0u, "the polygon-edge selection fixture had no hole");
  auto holeEdge = *mesh->getPolygon(holeRing).getEdgeIndexSet().begin();
  auto edgePosition = mesh->getEdge(holeEdge).getCentre();
  settings.meshEdgeSelectionDistance = 0.25f;
  require(document.getHoveredMeshSubObjectIndices(edgePosition, settings) ==
              std::vector<uint32_t>{holeRing},
          "hovering a hole edge in Polygon sub-mode did not resolve to its hole Ring");

  click = pointerAt(edgePosition);
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(document.getSelectedMeshRingIndices() == std::set<uint32_t>{holeRing},
          "clicking a hole edge in Polygon sub-mode did not select its hole Ring");
}

void draggingASelectedNestedRingDoesNotCycleToItsShell() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Polygon;
  settings.meshEdgeSelectionDistance = 0.25f;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  require(document.activateMesh(meshIndex), "could not activate the nested-Ring drag fixture");

  auto* mesh = document.getActiveMesh();
  auto shell = mesh->getFirstPolygonIndex();
  auto nested = mesh->getNextPolygonIndex(shell);
  require(!mesh->polygonIndexIterationFinished(nested),
          "the drag fixture did not contain a nested Ring");
  wp::Vector2 meshMin, meshMax;
  mesh->getExtents(meshMin, meshMax);
  auto centre = (meshMin + meshMax) / 2.0f;
  require(document.getHoveredMeshSubObjectIndices(centre, settings).size() > 1,
          "the nested-Ring drag fixture did not produce stacked hits");

  auto shellVertex = *mesh->getPolygon(shell).getVertexIndexSet().begin();
  auto nestedVertex = *mesh->getPolygon(nested).getVertexIndexSet().begin();
  auto shellStart = mesh->getVertex(shellVertex).getPosition();
  auto nestedStart = mesh->getVertex(nestedVertex).getPosition();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Polygon, {nested});

  editor::EditorInteraction interaction;
  auto press = pointerAt(centre);
  press.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, press);
  require(document.getSelectedMeshRingIndices() == std::set<uint32_t>{nested},
          "pressing a selected nested Ring cycled the selection to its shell");

  auto drag = pointerAt(centre + wp::Vector2{2.0f, 0.0f});
  drag.leftDown = true;
  drag.leftDragging = true;
  drag.dragDelta = {2.0f, 0.0f};
  interaction.updateSelection(&document, nullptr, settings, drag);
  interaction.updateDrag(&document, settings, drag);

  require(mesh->getVertex(nestedVertex).getPosition() ==
              nestedStart + wp::Vector2{2.0f, 0.0f},
          "dragging the selected nested Ring did not move it");
  require(mesh->getVertex(shellVertex).getPosition() == shellStart,
          "dragging the selected nested Ring moved its shell instead");

  auto release = pointerAt(centre + wp::Vector2{2.0f, 0.0f});
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  interaction.updateDrag(&document, settings, release);
  require(document.getSelectedMeshRingIndices() == std::set<uint32_t>{nested},
          "releasing a nested-Ring drag cycled the selection to its shell");
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

void meshDragMovesAffectedSubObjectsAsARigidGroup() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Edge;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();

  auto edgeIndex = mesh->getFirstEdgeIndex();
  auto const& edge = mesh->getEdge(edgeIndex);
  auto v0 = edge.getFirstVertex();
  auto v1 = edge.getSecondVertex();
  auto v0Start = mesh->getVertex(v0).getPosition();
  auto v1Start = mesh->getVertex(v1).getPosition();

  std::set<uint32_t> untouched;
  for (auto index = mesh->getFirstVertexIndex();
       !mesh->vertexIndexIterationFinished(index);
       index = mesh->getNextVertexIndex(index)) {
    if (index != v0 && index != v1) {
      untouched.insert(index);
    }
  }
  std::vector<wp::Vector2> untouchedStart;
  for (auto index : untouched) {
    untouchedStart.push_back(mesh->getVertex(index).getPosition());
  }

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Edge, {edgeIndex});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Edge);
  wp::Vector2 delta{1.0f, 0.5f};
  auto applied = document.updateMeshDrag(delta, false, 0.0f);
  require(applied == delta, "a valid rigid-group move was not applied in full");
  require(mesh->getVertex(v0).getPosition() == v0Start + delta,
          "the first edge endpoint did not move by the group delta");
  require(mesh->getVertex(v1).getPosition() == v1Start + delta,
          "the second edge endpoint did not move by the group delta");

  size_t i = 0;
  for (auto index : untouched) {
    require(mesh->getVertex(index).getPosition() == untouchedStart[i++],
            "a vertex outside the dragged selection moved");
  }
}

void draggingAnOuterRingMovesItsFullNestedHierarchyAsOneGroup() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Polygon;
  document.newDoc();
  auto* primitive = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {
          {
              {{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}},
              {{{-0.5f, -0.5f}}, {{-0.5f, 0.5f}}, {{0.5f, 0.5f}}, {{0.5f, -0.5f}}},
          },
          {
              {{{-0.25f, -0.25f}}, {{0.25f, -0.25f}}, {{0.25f, 0.25f}}, {{-0.25f, 0.25f}}},
              {{{-0.1f, -0.1f}}, {{-0.1f, 0.1f}}, {{0.1f, 0.1f}}, {{0.1f, -0.1f}}},
          },
          {{{{3.0f, -1.0f}}, {{5.0f, -1.0f}}, {{5.0f, 1.0f}}, {{3.0f, 1.0f}}}},
      });
  primitive->setSize(20.0f, 20.0f);
  primitive->updateVertexPositions();
  auto meshIndex = document.getWorld()->addPrimitive(primitive);
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();

  auto outerIndex = mesh->getFirstPolygonIndex();
  auto const& holes = mesh->getPolygon(outerIndex).getHoleIndices();
  require(holes.size() == 1, "the drag fixture did not contain one hole");
  auto holeIndex = holes.front();

  auto islandIndex = mesh->getNextPolygonIndex(holeIndex);
  require(!mesh->polygonIndexIterationFinished(islandIndex) &&
              !mesh->getPolygon(islandIndex).isHole(),
          "the drag fixture did not contain a filled island");
  auto islandHoleIndex = mesh->getNextPolygonIndex(islandIndex);
  require(!mesh->polygonIndexIterationFinished(islandHoleIndex) &&
              mesh->getPolygon(islandHoleIndex).isHole(),
          "the drag fixture did not contain the filled island's nested hole");
  auto unrelatedIndex = mesh->getNextPolygonIndex(islandHoleIndex);
  require(!mesh->polygonIndexIterationFinished(unrelatedIndex) &&
              !mesh->getPolygon(unrelatedIndex).isHole(),
          "the drag fixture did not contain an unrelated top-level Ring");

  std::map<uint32_t, wp::Vector2> starts;
  for (auto vertex : mesh->getPolygon(outerIndex).getVertexIndexSet()) {
    starts[vertex] = mesh->getVertex(vertex).getPosition();
  }
  for (auto vertex : mesh->getPolygon(holeIndex).getVertexIndexSet()) {
    starts[vertex] = mesh->getVertex(vertex).getPosition();
  }
  for (auto vertex : mesh->getPolygon(islandIndex).getVertexIndexSet()) {
    starts[vertex] = mesh->getVertex(vertex).getPosition();
  }
  for (auto vertex : mesh->getPolygon(islandHoleIndex).getVertexIndexSet()) {
    starts[vertex] = mesh->getVertex(vertex).getPosition();
  }
  std::map<uint32_t, wp::Vector2> unrelatedStarts;
  for (auto vertex : mesh->getPolygon(unrelatedIndex).getVertexIndexSet()) {
    unrelatedStarts[vertex] = mesh->getVertex(vertex).getPosition();
  }

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Polygon, {outerIndex});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Polygon);
  wp::Vector2 delta{2.0f, -3.0f};
  require(document.updateMeshDrag(delta, false, 0.0f) == delta,
          "the outer Ring and its hole did not accept a rigid-group move");
  for (auto const& [vertex, start] : starts) {
    require(mesh->getVertex(vertex).getPosition() == start + delta,
            "a nested hole or filled island did not move with its outer Ring");
  }
  for (auto const& [vertex, start] : unrelatedStarts) {
    require(mesh->getVertex(vertex).getPosition() == start,
            "an unrelated top-level Ring moved with the selected outer Ring");
  }
}

void meshDragClampsAtLastValidPositionOnSelfIntersection() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto vertexIndex = mesh->getFirstVertexIndex();
  auto start = mesh->getVertex(vertexIndex).getPosition();

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {vertexIndex});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Vertex);

  wp::Vector2 validDelta{0.5f, 0.5f};
  auto applied = document.updateMeshDrag(validDelta, false, 0.0f);
  require(applied == validDelta, "a small valid move was not applied in full");
  require(mesh->getVertex(vertexIndex).getPosition() == start + validDelta,
          "the vertex did not move to the valid position");

  // Find one of the two edges touching vertexIndex, and a "far" edge that
  // shares no vertex with it - the pairing the crossing check actually
  // tests. Aiming just past that far edge's midpoint, away from the near
  // edge's other endpoint, reliably crosses it and turns the quad into a
  // self-intersecting bowtie, regardless of the square's vertex ordering.
  uint32_t farEdge = ~0u;
  uint32_t nearNeighbour = ~0u;
  for (auto touchingEdge : mesh->getVertex(vertexIndex).getEdgeReferences()) {
    auto neighbour = static_cast<uint32_t>(
        mesh->getEdge(touchingEdge).getOtherVertex(vertexIndex));
    for (auto candidate = mesh->getFirstEdgeIndex();
         !mesh->edgeIndexIterationFinished(candidate);
         candidate = mesh->getNextEdgeIndex(candidate)) {
      auto const& candidateEdge = mesh->getEdge(candidate);
      auto c0 = static_cast<uint32_t>(candidateEdge.getFirstVertex());
      auto c1 = static_cast<uint32_t>(candidateEdge.getSecondVertex());
      if (c0 != vertexIndex && c1 != vertexIndex && c0 != neighbour && c1 != neighbour) {
        farEdge = candidate;
        nearNeighbour = neighbour;
        break;
      }
    }
    if (farEdge != ~0u) {
      break;
    }
  }
  require(farEdge != ~0u, "could not find a far edge to cross for the test mesh");

  auto const& fe = mesh->getEdge(farEdge);
  auto p0 = mesh->getVertex(fe.getFirstVertex()).getPosition();
  auto p1 = mesh->getVertex(fe.getSecondVertex()).getPosition();
  auto midpoint = (p0 + p1) * 0.5f;
  auto neighbourPos = mesh->getVertex(nearNeighbour).getPosition();
  auto target = midpoint + (midpoint - neighbourPos) * 0.1f;
  wp::Vector2 invalidDelta = target - start;

  auto clamped = document.updateMeshDrag(invalidDelta, false, 0.0f);
  require(clamped == applied,
          "a self-intersecting move was not clamped to the last valid delta");
  require(mesh->getVertex(vertexIndex).getPosition() == start + applied,
          "the mesh vertex did not stay at the last valid position");
}

void meshDragClampsAtLastValidPositionWhenAHoleWouldEscapeItsOuter() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();

  uint32_t holePolygonIndex = ~0u;
  for (auto polygonIndex = mesh->getFirstPolygonIndex();
       !mesh->polygonIndexIterationFinished(polygonIndex);
       polygonIndex = mesh->getNextPolygonIndex(polygonIndex)) {
    if (mesh->getPolygon(polygonIndex).isHole()) {
      holePolygonIndex = polygonIndex;
      break;
    }
  }
  require(holePolygonIndex != ~0u, "the test mesh has no hole Ring");
  auto holeVertices = mesh->getPolygon(holePolygonIndex).getVertexIndexSet();
  auto vertexIndex = *holeVertices.begin();
  auto start = mesh->getVertex(vertexIndex).getPosition();

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {vertexIndex});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Vertex);

  wp::Vector2 holeCentre;
  for (auto vertex : holeVertices) {
    holeCentre += mesh->getVertex(vertex).getPosition();
  }
  holeCentre /= static_cast<float>(holeVertices.size());
  auto validDelta = (start - holeCentre) * 0.1f;
  auto applied = document.updateMeshDrag(validDelta, false, 0.0f);
  require(applied == validDelta,
          "a hole vertex could not move outward while remaining inside its outer Ring");

  // Aim just past the midpoint of an edge that belongs to a different Ring
  // (the outer, since this mesh has only the two) - crossing it takes the
  // hole vertex out of the outer Ring it must stay inside.
  uint32_t farEdge = ~0u;
  uint32_t nearNeighbour = ~0u;
  for (auto touchingEdge : mesh->getVertex(vertexIndex).getEdgeReferences()) {
    auto neighbour = static_cast<uint32_t>(
        mesh->getEdge(touchingEdge).getOtherVertex(vertexIndex));
    for (auto candidate = mesh->getFirstEdgeIndex();
         !mesh->edgeIndexIterationFinished(candidate);
         candidate = mesh->getNextEdgeIndex(candidate)) {
      auto const& candidateEdge = mesh->getEdge(candidate);
      auto c0 = static_cast<uint32_t>(candidateEdge.getFirstVertex());
      auto c1 = static_cast<uint32_t>(candidateEdge.getSecondVertex());
      if (!holeVertices.contains(c0) && !holeVertices.contains(c1)) {
        farEdge = candidate;
        nearNeighbour = neighbour;
        break;
      }
    }
    if (farEdge != ~0u) {
      break;
    }
  }
  require(farEdge != ~0u, "could not find an outer edge to cross for the test mesh");

  auto const& fe = mesh->getEdge(farEdge);
  auto p0 = mesh->getVertex(fe.getFirstVertex()).getPosition();
  auto p1 = mesh->getVertex(fe.getSecondVertex()).getPosition();
  auto midpoint = (p0 + p1) * 0.5f;
  auto neighbourPos = mesh->getVertex(nearNeighbour).getPosition();
  auto target = midpoint + (midpoint - neighbourPos) * 0.1f;
  wp::Vector2 invalidDelta = target - start;

  auto clamped = document.updateMeshDrag(invalidDelta, false, 0.0f);
  require(clamped == applied,
          "a hole-escaping move was not clamped to the last valid delta");
  require(mesh->getVertex(vertexIndex).getPosition() == start + applied,
          "the hole vertex did not stay inside its outer at the last valid position");
}

void addingAMeshHoleInvalidatesTheParentTriangulation() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  require(document.activateMesh(meshIndex), "the hole invalidation Mesh did not activate");
  auto* mesh = document.getActiveMesh();
  auto outer = mesh->getFirstPolygonIndex();

  // Cache the solid triangulation before attaching the new hole, matching the
  // render path that has already drawn the enclosing Ring.
  wp::Vector2 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  wp::Vector2 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
  for (auto vertex : mesh->getPolygon(outer).getOrderedVertexIndices()) {
    auto const& p = mesh->getVertex(vertex).getPosition();
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
  }
  auto pointAt = [&](float x, float y) {
    return min + wp::Vector2{(max.x - min.x) * x, (max.y - min.y) * y};
  };
  auto settings = meshDrawSettings();
  settings.meshVertexPickRadius = 0.1f;
  require(document.armMeshDrawTool(settings), "the hole draw tool did not arm");
  require(document.placeMeshDrawVertex(pointAt(0.3f, 0.3f), settings) &&
              document.meshDrawCreatesHole(),
          "the first interior vertex did not establish a hole context");

  // The first point can activate and reconstruct the proxy. Cache the exact
  // parent triangulation which addHoleToPolygon will subsequently mutate.
  mesh = document.getActiveMesh();
  outer = document.getMeshDrawContainingRingIndex();
  auto triangleCountBefore =
      mesh->getPolygon(outer).createBasicTriangulation().getNumTriangles();
  require(triangleCountBefore > 0,
          "the enclosing Ring did not initially triangulate");
  require(document.placeMeshDrawVertex(pointAt(0.7f, 0.3f), settings) &&
              document.placeMeshDrawVertex(pointAt(0.7f, 0.7f), settings) &&
              document.placeMeshDrawVertex(pointAt(0.3f, 0.7f), settings),
          "the hole Ring rejected an interior vertex");
  auto* generator = new CountingWorldDataGenerator;
  document.getWorld()->setWorldDataGenerator(generator);
  require(document.closeMeshDrawRing() != nullptr,
          "the hole Ring did not close");

  auto* authored = dynamic_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  require(authored && authored->getVertices().size() == 1 &&
              authored->getVertices().front().size() == 2,
          "closing the hole did not add its Ring to the MeshPrimitive");
  require(generator->generationRequests == 1,
          "closing the hole did not request world regeneration from the polygon update");

  mesh = document.getActiveMesh();
  outer = mesh->getFirstPolygonIndex();
  auto triangleCountAfter =
      mesh->getPolygon(outer).createBasicTriangulation().getNumTriangles();
  require(triangleCountAfter != triangleCountBefore,
          "adding a hole left the enclosing Ring's cached triangulation stale");
}

void meshPolygonCommitRebuildsPrefabFieldInstancesBeforeRegeneration() {
  editor::Document document;
  document.newDoc();
  // Build the Layer before attaching it to the World, matching deserialization:
  // an unselected Prefab source is not in the derived Primitive cache when
  // World first binds the Layer.
  auto* layer = new bw::core::Layer(42, "Prefab Layer", BW_WORLD_SIZE, 64.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  auto defineIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Tile");
  definitions->setSelectedPrefab(prefab);
  layer->setActiveStep(defineIndex);
  auto* source = new bw::core::MeshPrimitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd,
      {{{{{-1.0f, -1.0f}}, {{1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{-1.0f, 1.0f}}}}});
  source->setSize(20.0f, 20.0f);
  source->setPosition({0.0f, 0.0f});
  source->updateVertexPositions();
  layer->addPrimitive(source);

  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(*layer, {0, 0}),
          "could not place the Mesh Prefab instance fixture");
  definitions->clearSelectedPrefab();
  layer->rebuild();

  document.getWorld()->addLayer(layer);
  document.getWorld()->setActiveLayer(layer);
  definitions->setSelectedPrefab(prefab);
  layer->setActiveStep(defineIndex);
  layer->rebuild();
  auto sourceIndex = prefab->getPrimitive(0)->getId();
  require(document.activateMesh(sourceIndex),
          "could not activate the Mesh Prefab source");

  auto findBuiltInstance = [&]() -> bw::core::MeshPrimitive* {
    for (uint32_t i = 0; i < layer->getNumPrimitives(); ++i) {
      if (layer->getOwningStepIndex(layer->getPrimitive(i)) == fieldIndex) {
        return dynamic_cast<bw::core::MeshPrimitive*>(layer->getPrimitive(i));
      }
    }
    return nullptr;
  };
  auto* builtBefore = findBuiltInstance();
  require(builtBefore && builtBefore->getVertices().size() == 1 &&
              builtBefore->getVertices().front().size() == 1,
          "the PrefabField fixture did not start with one solid Ring");

  auto* mesh = document.getActiveMesh();
  auto outer = mesh->getFirstPolygonIndex();
  wp::Vector2 min, max;
  mesh->getExtents(min, max);
  auto pointAt = [&](float x, float y) {
    return min + wp::Vector2{(max.x - min.x) * x, (max.y - min.y) * y};
  };
  auto settings = meshDrawSettings();
  settings.meshVertexPickRadius = 0.1f;
  require(document.armMeshDrawTool(settings) &&
              document.placeMeshDrawVertex(pointAt(0.3f, 0.3f), settings) &&
              document.getMeshDrawContainingRingIndex() == outer &&
              document.meshDrawCreatesHole() &&
              document.placeMeshDrawVertex(pointAt(0.7f, 0.3f), settings) &&
              document.placeMeshDrawVertex(pointAt(0.7f, 0.7f), settings) &&
              document.placeMeshDrawVertex(pointAt(0.3f, 0.7f), settings),
          "could not draw a hole in the Mesh Prefab source");

  auto* generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      document.getWorld()->getWorldDataGenerator());
  require(generator != nullptr, "the editor fixture has no dynamic generator");
  generator->setLayerSelection(bw::core::SelectLayer(layer->getId()));
  std::mutex generationMutex;
  std::condition_variable generationChanged;
  uint32_t completedGenerations = 0;
  auto callback = generator->registerGenerationCallback(
      [&](bw::core::DynamicWorldDataGenerator::GenerationDetails const& details) {
        if (details.state ==
            bw::core::DynamicWorldDataGenerator::GenerationState::Generated) {
          std::lock_guard lock(generationMutex);
          ++completedGenerations;
          generationChanged.notify_all();
        }
      });

  require(document.closeMeshDrawRing() != nullptr,
          "the Mesh Prefab hole did not close");
  {
    std::unique_lock lock(generationMutex);
    require(generationChanged.wait_for(
                lock, std::chrono::seconds(10),
                [&] { return completedGenerations != 0; }),
            "the Mesh polygon commit did not complete dynamic generation");
  }
  auto* builtAfter = findBuiltInstance();
  require(builtAfter && builtAfter->getVertices().size() == 1 &&
              builtAfter->getVertices().front().size() == 2,
          "polygon commit regenerated from a stale PrefabField instance");
  auto worldData = document.getWorld()->getWorldData();
  require(worldData->getContainingFaceIndex({0.0f, 0.0f}) == ~0u,
          "dynamic world data remained solid inside the committed Prefab hole");
  generator->unregisterGenerationCallback(callback);
}

void meshDragSnapsToGridBeforeValidating() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto vertexIndex = mesh->getFirstVertexIndex();
  auto start = mesh->getVertex(vertexIndex).getPosition();

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {vertexIndex});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Vertex);

  wp::Vector2 rawDelta{1.2f, 0.9f};
  auto applied = document.updateMeshDrag(rawDelta, true, 4.0f);

  auto target = start + rawDelta;
  wp::Vector2 expectedSnapped{
      std::round(target.x / 4.0f) * 4.0f, std::round(target.y / 4.0f) * 4.0f};
  auto expectedDelta = expectedSnapped - start;

  require(applied != rawDelta, "grid snapping did not adjust the raw delta");
  require(applied == expectedDelta,
          "the drag did not snap the anchor vertex onto the grid");
  require(mesh->getVertex(vertexIndex).getPosition() == expectedSnapped,
          "the validated position was not the snapped one");
}

void draggingADifferencePrimitiveDoesNotClearItsSelectionOnRelease() {
  editor::Document document;
  document.newDoc();
  auto index = addRectangle(document, {50.0f, 50.0f});
  auto* difference = document.getWorld()->getPrimitive(index);
  difference->setOperation(bw::core::Primitive::Operation::Difference);
  document.setSelectedPrimitiveIndices({index});
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Primitive;
  editor::EditorInteraction interaction;

  auto drag = pointerAt({500.0f, 500.0f});
  drag.leftDown = true;
  drag.leftDragging = true;
  drag.screenPosition += wp::Vector2{5.0f, 0.0f};
  drag.dragDelta = {5.0f, 0.0f};
  interaction.updateDrag(&document, settings, drag);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{index},
          "Difference Primitive selection was cleared during drag");
  auto release = drag;
  release.leftDown = false;
  release.leftDragging = false;
  release.leftReleased = true;
  interaction.updateSelection(&document, nullptr, settings, release);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{index},
          "Difference Primitive selection was cleared before drag commit");
  interaction.updateDrag(&document, settings, release);

  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{index},
          "releasing a Difference Primitive drag cleared its selection");
  editor::undo(&document);
}

void meshDragCommitIsOneUndoEntryAndUpdatesTheMeshPrimitive() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  settings.activeMeshPrimitiveIndex = meshIndex;
  auto* mesh = document.getActiveMesh();
  auto vertexIndex = mesh->getFirstVertexIndex();
  auto startWorld = mesh->getVertex(vertexIndex).getPosition();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {vertexIndex});
  document.setModified(false);
  auto const undoLevelsBefore = editor::getUndoLevels();

  editor::EditorInteraction interaction;

  auto drag1 = pointerAt(startWorld);
  drag1.leftDragging = true;
  drag1.dragDelta = {1.0f, -0.5f};
  interaction.updateDrag(&document, settings, drag1);

  auto drag2 = pointerAt(startWorld);
  drag2.leftDragging = true;
  drag2.dragDelta = {0.5f, 0.0f};
  interaction.updateDrag(&document, settings, drag2);

  auto release = pointerAt(startWorld);
  release.leftReleased = true;
  interaction.updateDrag(&document, settings, release);

  require(editor::getUndoLevels() == undoLevelsBefore + 1,
          "a single drag gesture produced more than one undo entry");
  require(document.isModified(), "committing a mesh drag did not mark the Document modified");

  // dragDelta.y is negated going from screen space to world space, matching
  // the Primitive-mode drag above it.
  wp::Vector2 expectedDelta{1.5f, 0.5f};
  auto expectedWorld = startWorld + expectedDelta;

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto committedProxy = primitive->createGeometryProxy();
  bool foundMovedVertex = false;
  for (auto index = committedProxy->getFirstVertexIndex();
       !committedProxy->vertexIndexIterationFinished(index);
       index = committedProxy->getNextVertexIndex(index)) {
    if (committedProxy->getVertex(index).getPosition().distanceToSq(expectedWorld) < 0.0001f) {
      foundMovedVertex = true;
      break;
    }
  }
  require(foundMovedVertex,
          "the committed MeshPrimitive geometry did not reflect the drag");

  editor::undo(&document);
  require(editor::getUndoLevels() == undoLevelsBefore,
          "undo after a mesh drag did not remove exactly one entry");

  auto* undonePrimitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto undoneProxy = undonePrimitive->createGeometryProxy();
  bool foundOriginalVertex = false;
  for (auto index = undoneProxy->getFirstVertexIndex();
       !undoneProxy->vertexIndexIterationFinished(index);
       index = undoneProxy->getNextVertexIndex(index)) {
    if (undoneProxy->getVertex(index).getPosition().distanceToSq(startWorld) < 0.0001f) {
      foundOriginalVertex = true;
      break;
    }
  }
  require(foundOriginalVertex, "undo did not restore the mesh vertex's original position");
}

void vertexDeletionHealsRingAndRefusesAtMinimumCount() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addPolygonMesh(
      document, {0.0f, 0.0f},
      {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  auto vertexToDelete = *mesh->getPolygon(ringIndex).getVertexIndexSet().begin();

  auto removed = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Vertex, {vertexToDelete});
  require(removed == 1, "a legal vertex delete on a 4-vertex Ring was refused");
  require(mesh->getPolygon(ringIndex).getVertexIndexSet().size() == 3,
          "the Ring did not heal to three vertices after the delete");

  auto remaining = mesh->getPolygon(ringIndex).getVertexIndexSet();
  auto secondVertex = *remaining.begin();
  auto refused = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Vertex, {secondVertex});
  require(refused == 0,
          "a vertex delete that would drop a Ring below three vertices was not refused");
  require(mesh->getPolygon(ringIndex).getVertexIndexSet().size() == 3,
          "a refused vertex delete still changed the Ring");
}

void edgeDeletionWeldsEndpointsAtMidpointAndRefusesAtMinimumCount() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addPolygonMesh(
      document, {0.0f, 0.0f},
      {{0.0f, -2.0f}, {2.0f, 0.0f}, {1.0f, 2.0f}, {-1.0f, 2.0f}, {-2.0f, 0.0f}});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  require(mesh->getPolygon(ringIndex).getVertexIndexSet().size() == 5,
          "the test mesh did not start with five vertices");

  auto edgeToDelete = mesh->getFirstEdgeIndex();
  auto const& edge = mesh->getEdge(edgeToDelete);
  auto v0 = static_cast<uint32_t>(edge.getFirstVertex());
  auto v1 = static_cast<uint32_t>(edge.getSecondVertex());
  auto midpoint = (mesh->getVertex(v0).getPosition() + mesh->getVertex(v1).getPosition()) / 2.0f;

  auto removed = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Edge, {edgeToDelete});
  require(removed == 1, "a legal edge delete on a 5-edge Ring was refused");
  require(mesh->getPolygon(ringIndex).getVertexIndexSet().size() == 4,
          "welding an edge did not reduce the Ring to four vertices");

  bool foundWeldedVertex = false;
  for (auto vertexIndex : mesh->getPolygon(ringIndex).getVertexIndexSet()) {
    if (mesh->getVertex(vertexIndex).getPosition().distanceToSq(midpoint) < 0.0001f) {
      foundWeldedVertex = true;
      break;
    }
  }
  require(foundWeldedVertex, "no surviving vertex sits at the deleted edge's midpoint");

  // Now down to a quad; deleting again brings the Ring to a triangle, which
  // remains above the three-edge minimum.
  auto secondEdge = mesh->getFirstEdgeIndex();
  require(document.deleteMeshSubObjects(
              editor::Settings::MeshSubMode::Edge, {secondEdge}) == 1,
          "a legal edge delete on a 4-edge Ring was refused");
  require(mesh->getPolygon(ringIndex).getNumEdges() == 3,
          "the Ring did not reduce to a triangle");

  auto thirdEdge = mesh->getFirstEdgeIndex();
  require(document.deleteMeshSubObjects(
              editor::Settings::MeshSubMode::Edge, {thirdEdge}) == 0,
          "an edge delete that would drop a Ring below three edges was not refused");
  require(mesh->getPolygon(ringIndex).getNumEdges() == 3,
          "a refused edge delete still changed the Ring");
}

void ringDeletionRemovesJustTheHoleWhenOthersRemain() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();

  uint32_t holeIndex = ~0u, outerIndex = ~0u;
  for (auto polygonIndex = mesh->getFirstPolygonIndex();
       !mesh->polygonIndexIterationFinished(polygonIndex);
       polygonIndex = mesh->getNextPolygonIndex(polygonIndex)) {
    if (mesh->getPolygon(polygonIndex).isHole()) {
      holeIndex = polygonIndex;
    } else {
      outerIndex = polygonIndex;
    }
  }
  require(holeIndex != ~0u && outerIndex != ~0u, "the test mesh did not have both an outer and a hole");

  auto removed = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Polygon, {holeIndex});
  require(removed == 1, "deleting a hole Ring was refused");
  require(document.getActiveMesh() != nullptr,
          "deleting a hole Ring deleted the whole MeshPrimitive");
  require(mesh->getPolygon(outerIndex).getHoleIndices().empty(),
          "the outer Ring still references the deleted hole");
}

void ringDeletionOfTheLastRingDeletesTheMeshPrimitive() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();

  auto removed = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Polygon, {ringIndex});
  require(removed == 1, "deleting the only Ring was refused");
  require(document.getActiveMesh() == nullptr,
          "deleting the last Ring did not clear the active mesh");
  require(document.getActiveMeshPrimitiveIndex() == ~0u,
          "deleting the last Ring did not clear the active mesh Primitive index");
}

void multiVertexDeleteProcessesAscendingAndReportsActualCount() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addPolygonMesh(
      document, {0.0f, 0.0f},
      {{0.0f, -2.0f}, {2.0f, 0.0f}, {1.0f, 2.0f}, {-1.0f, 2.0f}, {-2.0f, 0.0f}});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  auto allVertices = mesh->getPolygon(ringIndex).getVertexIndexSet();
  require(allVertices.size() == 5, "the test mesh did not start with five vertices");
  std::vector<wp::Vector2> startPositions;
  for (auto v : allVertices) {
    startPositions.push_back(mesh->getVertex(v).getPosition());
  }

  auto previewCount = document.previewMeshSubObjectDeletionCount(
      editor::Settings::MeshSubMode::Vertex, allVertices);
  auto removed = document.deleteMeshSubObjects(
      editor::Settings::MeshSubMode::Vertex, allVertices);
  require(previewCount == removed,
          "the preview count did not match the number actually removed");

  // Ascending order: index 0 (5 > 3) and index 1 (4 > 3) succeed, then the
  // Ring is down to three vertices and every later index is refused. The
  // delete compacts the mesh on the way out, so the surviving vertices are
  // identified by position (their original indices no longer apply).
  require(removed == 2, "a partial multi-delete did not stop exactly at the minimum-count rule");
  auto remaining = mesh->getPolygon(ringIndex).getVertexIndexSet();
  require(remaining.size() == 3, "the Ring did not end with exactly three vertices");

  auto survives = [&](wp::Vector2 const& pos) {
    for (auto v : remaining) {
      if (mesh->getVertex(v).getPosition().distanceToSq(pos) < 0.0001f) {
        return true;
      }
    }
    return false;
  };
  require(!survives(startPositions[0]), "the lowest-index vertex was not the first one removed");
  require(!survives(startPositions[1]), "the second-lowest-index vertex was not the second one removed");
  require(survives(startPositions[2]) && survives(startPositions[3]) && survives(startPositions[4]),
          "a vertex outside the ascending-order deletes was unexpectedly removed");
}

void meshSubObjectDeleteIsOneUndoEntry() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  document.newDoc();
  auto meshIndex = addPolygonMesh(
      document, {0.0f, 0.0f},
      {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  auto vertexToDelete = *mesh->getPolygon(ringIndex).getVertexIndexSet().begin();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Vertex, {vertexToDelete});
  document.setModified(false);
  auto const undoLevelsBefore = editor::getUndoLevels();

  editor::transactUndoableAction(
      &document, "Delete 1 Mesh Vertex(es)",
      std::bind(
          editor::deleteMeshSubObjects, std::placeholders::_1,
          editor::Settings::MeshSubMode::Vertex, std::set<uint32_t>{vertexToDelete}));

  require(editor::getUndoLevels() == undoLevelsBefore + 1,
          "a mesh sub-object delete produced more than one undo entry");
  require(document.isModified(), "the delete did not mark the Document modified");

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto committedProxy = primitive->createGeometryProxy();
  require(committedProxy->getPolygon(committedProxy->getFirstPolygonIndex())
                  .getVertexIndexSet()
                  .size() == 3,
          "the committed MeshPrimitive geometry did not reflect the delete");

  editor::undo(&document);
  require(editor::getUndoLevels() == undoLevelsBefore,
          "undo after a mesh delete did not remove exactly one entry");

  auto* undonePrimitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto undoneProxy = undonePrimitive->createGeometryProxy();
  require(undoneProxy->getPolygon(undoneProxy->getFirstPolygonIndex())
                  .getVertexIndexSet()
                  .size() == 4,
          "undo did not restore the deleted vertex");
}

void edgeSplitInsertsUnsnappedMidpointAndSelectsBothHalves() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addPolygonMesh(
      document, {0.0f, 0.0f},
      {{0.0f, 0.0f}, {7.0f, 3.0f}, {7.0f, 10.0f}, {0.0f, 10.0f}});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  require(mesh->getPolygon(ringIndex).getNumEdges() == 4, "the test mesh did not start with four edges");

  // The first edge, {0,0}-{7,3}, has a midpoint off any reasonable grid.
  auto edgeToSplit = mesh->getFirstEdgeIndex();
  auto const& edge = mesh->getEdge(edgeToSplit);
  auto expectedMidpoint =
      (mesh->getVertex(edge.getFirstVertex()).getPosition() +
       mesh->getVertex(edge.getSecondVertex()).getPosition()) /
      2.0f;

  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Edge, {edgeToSplit});
  auto split = document.splitMeshEdges({edgeToSplit});
  require(split == 1, "splitting one selected edge did not report one split");
  require(mesh->getPolygon(ringIndex).getNumEdges() == 5,
          "splitting an edge did not add exactly one edge to the Ring");
  require(mesh->getPolygon(ringIndex).getVertexIndexSet().size() == 5,
          "splitting an edge did not add exactly one vertex to the Ring");

  bool foundMidpointVertex = false;
  for (auto vertexIndex : mesh->getPolygon(ringIndex).getVertexIndexSet()) {
    if (mesh->getVertex(vertexIndex).getPosition().distanceToSq(expectedMidpoint) < 0.0001f) {
      foundMidpointVertex = true;
      break;
    }
  }
  require(foundMidpointVertex, "the new vertex was not placed exactly at the edge's unsnapped midpoint");

  auto const& selected = document.getSelectedMeshEdgeIndices();
  require(selected.size() == 2, "splitting one edge did not leave exactly two edges selected");
  for (auto selectedEdge : selected) {
    auto const& e = mesh->getEdge(selectedEdge);
    auto v0 = mesh->getVertex(e.getFirstVertex()).getPosition();
    auto v1 = mesh->getVertex(e.getSecondVertex()).getPosition();
    require(v0.distanceToSq(expectedMidpoint) < 0.0001f || v1.distanceToSq(expectedMidpoint) < 0.0001f,
            "a selected edge after the split does not touch the new midpoint vertex");
  }
}

void repeatedEdgeSplitSubdividesIntoFourSegments() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  require(mesh->getPolygon(ringIndex).getNumEdges() == 4, "the test mesh did not start with four edges");

  auto firstEdge = mesh->getFirstEdgeIndex();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Edge, {firstEdge});

  document.splitMeshEdges(document.getSelectedMeshEdgeIndices());
  require(mesh->getPolygon(ringIndex).getNumEdges() == 5,
          "the first split did not turn one edge of the Ring into two");
  require(document.getSelectedMeshEdgeIndices().size() == 2,
          "the first split did not leave both halves selected");

  document.splitMeshEdges(document.getSelectedMeshEdgeIndices());
  // The other three original edges of the square are untouched throughout;
  // only the one repeatedly-split edge's region grows from two segments to
  // four, so the Ring's total edge count is the other three plus those four.
  require(mesh->getPolygon(ringIndex).getNumEdges() == 7,
          "resplitting both halves did not turn two segments into four");
  require(document.getSelectedMeshEdgeIndices().size() == 4,
          "resplitting both halves did not leave all four quarters selected");
}

void edgeSplitIsOneUndoEntry() {
  editor::Document document;
  editor::Settings settings;
  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Edge;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  document.activateMesh(meshIndex);
  auto* mesh = document.getActiveMesh();
  auto ringIndex = mesh->getFirstPolygonIndex();
  auto edgeToSplit = mesh->getFirstEdgeIndex();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Edge, {edgeToSplit});
  document.setModified(false);
  auto const undoLevelsBefore = editor::getUndoLevels();

  editor::transactUndoableAction(
      &document, "Split 1 Mesh Edge(s)",
      std::bind(editor::splitMeshEdges, std::placeholders::_1, std::set<uint32_t>{edgeToSplit}));

  require(editor::getUndoLevels() == undoLevelsBefore + 1,
          "a mesh edge split produced more than one undo entry");
  require(document.isModified(), "the split did not mark the Document modified");

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto committedProxy = primitive->createGeometryProxy();
  require(committedProxy->getPolygon(committedProxy->getFirstPolygonIndex()).getNumEdges() == 5,
          "the committed MeshPrimitive geometry did not reflect the split");

  editor::undo(&document);
  require(editor::getUndoLevels() == undoLevelsBefore,
          "undo after a mesh edge split did not remove exactly one entry");

  auto* undonePrimitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  auto undoneProxy = undonePrimitive->createGeometryProxy();
  require(undoneProxy->getPolygon(undoneProxy->getFirstPolygonIndex()).getNumEdges() == 4,
          "undo did not restore the Ring to its unsplit edge count");
}

void drawToolArmsOnlyInVertexSubModeOnAnAcceptingStep() {
  editor::Document document;
  auto settings = meshDrawSettings();
  document.newDoc();

  settings.mode = editor::Settings::Mode::Primitive;
  require(!document.armMeshDrawTool(settings) && !document.meshDrawToolArmed(),
          "the draw tool armed outside Mesh mode");

  settings.mode = editor::Settings::Mode::Mesh;
  settings.meshSubMode = editor::Settings::MeshSubMode::Edge;
  require(!document.armMeshDrawTool(settings) &&
              document.meshDrawToolUnavailableReason(settings).find("Vertex sub-mode") !=
                  std::string::npos,
          "the draw tool armed outside Vertex sub-mode");

  settings.meshSubMode = editor::Settings::MeshSubMode::Vertex;
  require(document.meshDrawToolUnavailableReason(settings).empty() &&
              document.armMeshDrawTool(settings) && document.meshDrawToolArmed(),
          "the draw tool did not arm in Vertex sub-mode on an accepting step");

  document.disarmMeshDrawTool();
  auto* layer = document.getWorld()->getActiveLayer();
  layer->setActiveStep(layer->addStep(new RefusingStep()));

  require(document.meshDrawToolUnavailableReason(settings).find("does not accept new Primitives") !=
                  std::string::npos &&
              !document.armMeshDrawTool(settings),
          "the draw tool armed on a step that refuses new Primitives");
}

void drawClicksPlaceGridSnappedVerticesAndRefuseToCloseBelowThree() {
  editor::Document document;
  auto settings = meshDrawSettings();
  settings.showGrid = true;
  settings.gridSize = 10.0f;
  document.newDoc();
  require(document.armMeshDrawTool(settings), "the draw tool did not arm");
  editor::EditorInteraction interaction;

  auto click = [&](wp::Vector2 const& position) {
    auto input = pointerAt(position);
    input.leftClicked = true;
    interaction.updateSelection(&document, nullptr, settings, input);
  };
  auto placedAt = [&](size_t index, wp::Vector2 const& expected) {
    return document.getMeshDrawVertices()[index].distanceToSq(expected) < 0.0001f;
  };

  click({102.0f, 97.0f});
  require(document.getMeshDrawVertices().size() == 1 && placedAt(0, {100.0f, 100.0f}),
          "a draw click did not place a vertex snapped to the grid");

  click({141.0f, 98.0f});
  require(document.getMeshDrawVertices().size() == 2 && placedAt(1, {140.0f, 100.0f}),
          "the second draw click did not place a grid-snapped vertex");

  // The first vertex with only two placed can neither close the shape nor
  // stack a third vertex on top of itself.
  click({101.0f, 101.0f});
  require(document.getMeshDrawVertices().size() == 2 && !document.getActiveMesh(),
          "the first vertex closed or stacked a vertex below three vertices");

  click({139.0f, 142.0f});
  require(document.getMeshDrawVertices().size() == 3, "the third draw click did not place a vertex");
  require(document.meshDrawClickWouldClose({100.0f, 100.0f}, settings),
          "the first vertex refused to close a three-vertex Ring");

  click({101.0f, 99.0f});
  require(!document.meshDrawToolArmed() && document.getActiveMesh(),
          "clicking the first vertex did not close the shape into an active mesh");
}

void backspaceStepsBackAndEscapeIsTwoStage() {
  editor::Document document;
  auto settings = meshDrawSettings();
  document.newDoc();
  require(document.armMeshDrawTool(settings), "the draw tool did not arm");

  document.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  document.placeMeshDrawVertex({100.0f, 0.0f}, settings);
  require(document.removeLastMeshDrawVertex() && document.getMeshDrawVertices().size() == 1,
          "Backspace did not remove the last placed vertex");

  require(document.escapeMeshDraw() && document.meshDrawToolArmed() &&
              document.getMeshDrawVertices().empty(),
          "the first Esc did not discard the in-progress Ring while staying armed");
  require(document.escapeMeshDraw() && !document.meshDrawToolArmed(),
          "the second Esc did not disarm the draw tool");
  require(!document.escapeMeshDraw(), "Esc acted with the draw tool already disarmed");
}

void switchingSubModeOrLeavingMeshModeDisarmsAndDiscards() {
  editor::Document document;
  auto settings = meshDrawSettings();
  document.newDoc();

  require(document.armMeshDrawTool(settings), "the draw tool did not arm");
  document.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  editor::setMeshSubMode(&document, settings, editor::Settings::MeshSubMode::Edge);
  require(!document.meshDrawToolArmed() && document.getMeshDrawVertices().empty(),
          "switching sub-mode did not disarm the draw tool and discard its Ring");

  editor::setMeshSubMode(&document, settings, editor::Settings::MeshSubMode::Vertex);
  require(document.armMeshDrawTool(settings), "the draw tool did not re-arm");
  document.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  editor::setEditorMode(&document, settings, editor::Settings::Mode::Primitive);
  require(!document.meshDrawToolArmed() && document.getMeshDrawVertices().empty(),
          "leaving Mesh mode did not disarm the draw tool and discard its Ring");
}

void closingADrawnRingCreatesAMeshPrimitiveWithCanonicalWinding() {
  editor::Document document;
  auto settings = meshDrawSettings();
  document.newDoc();

  auto* ghost = document.getGhost();
  ghost->setOperation(bw::core::Primitive::Operation::Difference);
  ghost->setFillRule(bw::core::Primitive::FillRule::NonZero);
  ghost->setPriority(7);

  // Drawn clockwise: the closed Ring must still come out anticlockwise.
  require(document.armMeshDrawTool(settings), "the draw tool did not arm");
  document.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  document.placeMeshDrawVertex({0.0f, 100.0f}, settings);
  document.placeMeshDrawVertex({100.0f, 100.0f}, settings);
  document.placeMeshDrawVertex({100.0f, 0.0f}, settings);

  auto* created = document.closeMeshDrawRing();
  require(created != nullptr, "closing a four-vertex Ring did not create a Primitive");
  require(!document.meshDrawToolArmed() && document.getMeshDrawVertices().empty(),
          "closing the shape did not disarm the draw tool");
  require(document.getActiveMesh() && document.getActiveMeshPrimitiveIndex() == created->getId(),
          "closing the shape did not make the new mesh the active one");
  require(document.getSelectedMeshVertexIndices().empty() &&
              document.getSelectedMeshEdgeIndices().empty() &&
              document.getSelectedMeshRingIndices().empty() &&
              document.getSelectedPrimitiveIndices().empty(),
          "closing the shape left something selected");
  require(created->getOperation() == bw::core::Primitive::Operation::Difference &&
              created->getFillRule() == bw::core::Primitive::FillRule::NonZero &&
              created->getPriority() == 7,
          "the new MeshPrimitive did not take the Mesh panel's operation, fill rule and priority");

  auto clockwiseRing = activeRingPositions(document);
  require(clockwiseRing.size() == 4, "the new MeshPrimitive did not carry the four drawn vertices");
  require(twiceSignedArea(clockwiseRing) > 0.0f,
          "a clockwise-drawn Ring was not forced to the canonical winding");

  // The same shape drawn the other way round produces the same winding.
  editor::Document other;
  other.newDoc();
  require(other.armMeshDrawTool(settings), "the draw tool did not arm");
  other.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  other.placeMeshDrawVertex({100.0f, 0.0f}, settings);
  other.placeMeshDrawVertex({100.0f, 100.0f}, settings);
  other.placeMeshDrawVertex({0.0f, 100.0f}, settings);
  require(other.closeMeshDrawRing() != nullptr, "closing an anticlockwise Ring did not create a Primitive");
  require(twiceSignedArea(activeRingPositions(other)) > 0.0f,
          "an anticlockwise-drawn Ring did not keep the canonical winding");
}

void drawingContextCreatesHolesAndFilledIslands() {
  auto settings = meshDrawSettings();

  editor::Document holeDocument;
  holeDocument.newDoc();
  auto holeMeshIndex = addMeshWithHole(holeDocument);
  require(holeDocument.armMeshDrawTool(settings), "the hole draw tool did not arm");
  auto placedHoleFirst = holeDocument.placeMeshDrawVertex({-8.0f, 92.0f}, settings);
  require(placedHoleFirst &&
              holeDocument.meshDrawCreatesHole() &&
              holeDocument.getActiveMeshPrimitiveIndex() == holeMeshIndex,
          "a first vertex in a filled region did not fix a hole context");
  auto containingRing = holeDocument.getMeshDrawContainingRingIndex();
  holeDocument.placeMeshDrawVertex({8.0f, 92.0f}, settings);
  holeDocument.placeMeshDrawVertex({0.0f, 94.0f}, settings);
  require(holeDocument.closeMeshDrawRing() != nullptr,
          "a Ring inside a filled region did not close as a hole");
  auto* holePrimitive = static_cast<bw::core::MeshPrimitive*>(
      holeDocument.getWorld()->getPrimitive(holeMeshIndex));
  require(holePrimitive->getVertices().size() == 1 &&
              holePrimitive->getVertices().front().size() == 3,
          "the drawn hole was not stored on its containing ComplexPolygon");

  editor::Document islandDocument;
  islandDocument.newDoc();
  auto islandMeshIndex = addMeshWithHole(islandDocument);
  require(islandDocument.armMeshDrawTool(settings), "the island draw tool did not arm");
  require(islandDocument.placeMeshDrawVertex({-4.0f, 97.0f}, settings) &&
              islandDocument.meshDrawCreatesIsland() &&
              islandDocument.getMeshDrawContainingRingIndex() != containingRing,
          "a first vertex in a hole did not fix a filled-island context");
  islandDocument.placeMeshDrawVertex({4.0f, 97.0f}, settings);
  islandDocument.placeMeshDrawVertex({0.0f, 104.0f}, settings);
  require(islandDocument.closeMeshDrawRing() != nullptr,
          "a Ring inside a hole did not close as a filled island");
  auto* islandPrimitive = static_cast<bw::core::MeshPrimitive*>(
      islandDocument.getWorld()->getPrimitive(islandMeshIndex));
  require(islandPrimitive->getVertices().size() == 2 &&
              islandPrimitive->getVertices()[1].size() == 1,
          "the filled island was not stored as its own top-level ComplexPolygon");

  auto path = std::filesystem::temp_directory_path() /
              "boolean-world-ticket-184-island.world.yaml";
  islandDocument.saveDocAs(path.string());
  editor::Document reloaded;
  require(reloaded.openDoc(path.string()), "the island test World did not reload");
  std::filesystem::remove(path);
  uint32_t reloadedMeshIndex = ~0u;
  bw::core::MeshPrimitive* reloadedPrimitive = nullptr;
  for (uint32_t i = 0; i < reloaded.getWorld()->getNumPrimitives(); ++i) {
    auto* candidate = dynamic_cast<bw::core::MeshPrimitive*>(
        reloaded.getWorld()->getPrimitive(i));
    if (candidate && !candidate->hasFlag(BW_PRIMITIVE_GHOST_FLAG)) {
      reloadedMeshIndex = i;
      reloadedPrimitive = candidate;
      break;
    }
  }
  require(reloadedPrimitive && reloadedPrimitive->getVertices().size() == 2,
          "the filled island did not survive save/reload as a top-level polygon");
  require(reloaded.activateMesh(reloadedMeshIndex),
          "the reloaded island MeshPrimitive did not rebuild its proxy");
  uint32_t topLevel = 0;
  for (auto index = reloaded.getActiveMesh()->getFirstPolygonIndex();
       !reloaded.getActiveMesh()->polygonIndexIterationFinished(index);
       index = reloaded.getActiveMesh()->getNextPolygonIndex(index)) {
    if (!reloaded.getActiveMesh()->getPolygon(index).isHole()) {
      ++topLevel;
    }
  }
  require(topLevel == 2, "the reloaded proxy did not re-derive the filled island");
}

void drawingMultipleHolesKeepsThemOnTheSameComplexPolygon() {
  editor::Document document;
  auto settings = meshDrawSettings();
  settings.meshVertexPickRadius = 0.1f;
  document.newDoc();
  auto meshIndex = addMesh(document, {0.0f, 0.0f});
  require(document.activateMesh(meshIndex), "the multiple-hole Mesh did not activate");

  wp::Vector2 min, max;
  document.getActiveMesh()->getExtents(min, max);
  auto pointAt = [&](float x, float y) {
    return wp::Vector2{
        min.x + (max.x - min.x) * x,
        min.y + (max.y - min.y) * y};
  };
  auto drawHole = [&](wp::Vector2 first, wp::Vector2 second, wp::Vector2 third) {
    require(document.armMeshDrawTool(settings), "the multiple-hole draw tool did not arm");
    require(document.placeMeshDrawVertex(first, settings) && document.meshDrawCreatesHole(),
            "a hole gesture did not capture the outer Ring context");
    require(document.placeMeshDrawVertex(second, settings) &&
                document.placeMeshDrawVertex(third, settings),
            "a hole gesture rejected an interior vertex");
    require(document.closeMeshDrawRing() != nullptr,
            "a hole gesture did not close");
  };

  drawHole(pointAt(0.15f, 0.25f), pointAt(0.4f, 0.25f), pointAt(0.275f, 0.7f));
  drawHole(pointAt(0.6f, 0.25f), pointAt(0.85f, 0.25f), pointAt(0.725f, 0.7f));

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  require(primitive->getVertices().size() == 1 &&
              primitive->getVertices().front().size() == 3,
          "the two holes were not stored on the same ComplexPolygon");

  auto* mesh = document.getActiveMesh();
  auto outerIndex = mesh->getFirstPolygonIndex();
  auto const& holes = mesh->getPolygon(outerIndex).getHoleIndices();
  require(holes.size() == 2, "the proxy did not retain both independent holes");
  auto firstHole = holes.front();
  auto secondHole = holes.back();
  require(firstHole != secondHole &&
              mesh->getPolygon(firstHole).isHole() &&
              mesh->getPolygon(secondHole).isHole(),
          "the two drawn holes were not retained as independent Rings");
}

void fillingASelectedHoleCreatesASolidAlongsideExistingIslands() {
  editor::Document document;
  auto settings = meshDrawSettings();
  settings.meshVertexPickRadius = 0.1f;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  require(document.activateMesh(meshIndex), "the fill-hole Mesh did not activate");

  auto* mesh = document.getActiveMesh();
  auto outerIndex = mesh->getFirstPolygonIndex();
  auto holeIndex = mesh->getPolygon(outerIndex).getHoleIndices().front();
  std::vector<wp::Vector2> holePositions;
  for (auto vertex : mesh->getPolygon(holeIndex).getOrderedVertexIndices()) {
    holePositions.push_back(mesh->getVertex(vertex).getPosition());
  }
  auto holeCentre = wp::Vector2{};
  for (auto const& position : holePositions) {
    holeCentre += position;
  }
  holeCentre /= static_cast<float>(holePositions.size());

  require(document.armMeshDrawTool(settings), "the island draw tool did not arm");
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{-2.0f, -2.0f}, settings);
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{2.0f, -2.0f}, settings);
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{0.0f, 2.0f}, settings);
  require(document.closeMeshDrawRing() != nullptr,
          "the existing island fixture did not close");

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  require(primitive->getVertices().size() == 2,
          "the existing island was not top-level before filling the hole");
  auto islandBefore = primitive->getVertices()[1];

  require(document.fillMeshHole(holeIndex),
          "the selected hole could not be filled");
  require(primitive->getVertices().size() == 3 &&
              primitive->getVertices()[0].size() == 2 &&
              primitive->getVertices()[1].size() == islandBefore.size() &&
              primitive->getVertices()[1][0].size() == islandBefore[0].size() &&
              primitive->getVertices()[1][0][0].p == islandBefore[0][0].p &&
              primitive->getVertices()[2].size() == 2,
          "filling the hole did not create only the gap around its existing island");
  require(document.getSelectedMeshRingIndices().size() == 1 &&
              !document.getActiveMesh()
                   ->getPolygon(*document.getSelectedMeshRingIndices().begin())
                   .isHole(),
          "the newly filled Ring was not selected as a solid polygon");

  auto filledRing = *document.getSelectedMeshRingIndices().begin();
  auto const& gapHoles = document.getActiveMesh()
                             ->getPolygon(filledRing)
                             .getHoleIndices();
  require(document.getActiveMesh()->getPolygon(holeIndex).getEdgeIndexSet() ==
                  document.getActiveMesh()->getPolygon(filledRing).getEdgeIndexSet() &&
              gapHoles.size() == 1 &&
              document.getActiveMesh()->getPolygon(gapHoles.front()).getEdgeIndexSet() ==
                  document.getActiveMesh()
                      ->getPolygon(document.getActiveMesh()
                                       ->getNextPolygonIndex(holeIndex))
                      .getEdgeIndexSet(),
          "the gap duplicated boundaries instead of sharing the existing topology");

  settings.meshSubMode = editor::Settings::MeshSubMode::Polygon;
  settings.meshEdgeSelectionDistance = 0.01f;
  auto sharedEdge = document.getActiveMesh()
                        ->getPolygon(filledRing)
                        .getEdges()
                        .front();
  auto edgeMidpoint =
      (document.getActiveMesh()->getVertex(sharedEdge.v0).getPosition() +
       document.getActiveMesh()->getVertex(sharedEdge.v1).getPosition()) /
      2.0f;
  auto weldedHover =
      document.getHoveredMeshSubObjectIndices(edgeMidpoint, settings);
  require(weldedHover == std::vector<uint32_t>{filledRing},
          "the welded hole was selectable instead of only its filled island (hover count " +
              std::to_string(weldedHover.size()) + ")");

  auto weldedVertex = document.getActiveMesh()
                          ->getPolygon(holeIndex)
                          .getOrderedVertexIndices()
                          .front();
  auto weldedStart = document.getActiveMesh()->getVertex(weldedVertex).getPosition();
  document.setSelectedMeshSubObjectIndices(
      editor::Settings::MeshSubMode::Polygon, {filledRing});
  document.beginMeshDrag(editor::Settings::MeshSubMode::Polygon);
  auto weldedDelta = wp::Vector2{3.0f, 4.0f};
  require(document.updateMeshDrag(weldedDelta, false, 0.0f) == weldedDelta &&
              document.getActiveMesh()->getVertex(weldedVertex).getPosition() ==
                  weldedStart + weldedDelta,
          "moving the island did not move its welded hole boundary");
  require(document.commitMeshDrag(), "the welded island move did not commit");
  document.endMeshDrag();
  auto islandAfterMove =
      primitive->getVertices()[1][0][0].p;

  auto path = std::filesystem::temp_directory_path() /
              "boolean-world-fill-hole-topology.world.yaml";
  document.saveDocAs(path.string());
  editor::Document reloaded;
  require(reloaded.openDoc(path.string()), "the filled-hole World did not reload");
  std::filesystem::remove(path);
  uint32_t reloadedMeshIndex = ~0u;
  for (uint32_t i = 0; i < reloaded.getWorld()->getNumPrimitives(); ++i) {
    auto* candidate = dynamic_cast<bw::core::MeshPrimitive*>(
        reloaded.getWorld()->getPrimitive(i));
    if (candidate && !candidate->hasFlag(BW_PRIMITIVE_GHOST_FLAG)) {
      reloadedMeshIndex = i;
      break;
    }
  }
  require(reloaded.activateMesh(reloadedMeshIndex),
          "the reloaded filled-hole Mesh did not activate");
  auto* reloadedMesh = reloaded.getActiveMesh();
  auto reloadedOuter = reloadedMesh->getFirstPolygonIndex();
  auto reloadedOriginalHole =
      reloadedMesh->getPolygon(reloadedOuter).getHoleIndices().front();
  uint32_t reloadedGap = ~0u;
  for (auto polygon = reloadedMesh->getNextPolygonIndex(reloadedOriginalHole);
       !reloadedMesh->polygonIndexIterationFinished(polygon);
       polygon = reloadedMesh->getNextPolygonIndex(polygon)) {
    if (!reloadedMesh->getPolygon(polygon).isHole() &&
        reloadedMesh->getPolygon(polygon).getNumEdges() ==
            reloadedMesh->getPolygon(reloadedOriginalHole).getNumEdges()) {
      reloadedGap = polygon;
    }
  }
  require(reloadedGap != ~0u &&
              reloadedMesh->getPolygon(reloadedGap).getEdgeIndexSet() ==
                  reloadedMesh->getPolygon(reloadedOriginalHole).getEdgeIndexSet(),
          "save/reload did not re-derive the shared gap boundary");

  require(document.deleteMeshSubObjects(
              editor::Settings::MeshSubMode::Polygon, {filledRing}) == 1 &&
              primitive->getVertices().size() == 2 &&
              primitive->getVertices()[0].size() == 2 &&
              primitive->getVertices()[1].size() == islandBefore.size() &&
              primitive->getVertices()[1][0][0].p == islandAfterMove,
          "deleting the gap polygon did not restore the previous hole and island topology");
}

void decomposingAMeshCreatesOrderedRingPrimitives() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  require(document.activateMesh(meshIndex),
          "the decomposition Mesh did not activate");
  auto* proxy = document.getActiveMesh();
  auto outerRing = proxy->getFirstPolygonIndex();
  auto holeRing = proxy->getPolygon(outerRing).getHoleIndices().front();
  wp::Vector2 holeCentre{};
  auto holeVertices = proxy->getPolygon(holeRing).getOrderedVertexIndices();
  for (auto vertex : holeVertices) {
    holeCentre += proxy->getVertex(vertex).getPosition();
  }
  holeCentre /= float(holeVertices.size());
  auto settings = meshDrawSettings();
  settings.meshVertexPickRadius = 0.1f;
  require(document.armMeshDrawTool(settings),
          "the decomposition island draw tool did not arm");
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{-2.0f, -2.0f}, settings);
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{2.0f, -2.0f}, settings);
  document.placeMeshDrawVertex(holeCentre + wp::Vector2{0.0f, 2.0f}, settings);
  require(document.closeMeshDrawRing() != nullptr,
          "the decomposition island did not close");

  auto* source = static_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(meshIndex));
  source->setPriority(9);
  auto properties = source->getProperties();
  properties.floorZ = -12.0f;
  properties.ceilingZ = 72.0f;
  properties.floorMaterialIndex = 17;
  source->setProperties(properties);

  require(editor::decomposeMeshPrimitive(&document, meshIndex),
          "the multi-Ring MeshPrimitive did not decompose");
  auto const& selected = document.getSelectedPrimitiveIndices();
  require(selected.size() == 3,
          "decomposition did not select one Primitive per Ring");
  auto selection = selected.begin();
  auto* outer = dynamic_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(*selection++));
  auto* hole = dynamic_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(*selection++));
  auto* island = dynamic_cast<bw::core::MeshPrimitive*>(
      document.getWorld()->getPrimitive(*selection));
  require(outer && hole && island && outer->getVertices().size() == 1 &&
              outer->getVertices()[0].size() == 1 &&
              hole->getVertices().size() == 1 &&
              hole->getVertices()[0].size() == 1 &&
              island->getVertices().size() == 1 &&
              island->getVertices()[0].size() == 1,
          "a decomposed Primitive retained more than one Ring");
  require(outer->getOperation() == bw::core::Primitive::Operation::Union &&
              hole->getOperation() == bw::core::Primitive::Operation::Difference &&
              island->getOperation() == bw::core::Primitive::Operation::Union &&
              outer->getPriority() == 9 && hole->getPriority() == 10 &&
              island->getPriority() == 11,
          "decomposed Rings did not receive containment-ordered operations and priorities");
  require(outer->getProperties().floorZ == properties.floorZ &&
              hole->getProperties().ceilingZ == properties.ceilingZ &&
              outer->getProperties().floorMaterialIndex == 17 &&
              hole->getProperties().floorMaterialIndex == 17 &&
              island->getProperties().floorMaterialIndex == 17,
          "decomposition did not preserve the source properties and materials");
}

void deletingAWeldedVertexHealsTheHoleAndIsland() {
  editor::Document document;
  document.newDoc();
  auto meshIndex = addMeshWithHole(document);
  require(document.activateMesh(meshIndex),
          "the welded-vertex Mesh did not activate");

  auto* mesh = document.getActiveMesh();
  auto outer = mesh->getFirstPolygonIndex();
  auto hole = mesh->getPolygon(outer).getHoleIndices().front();
  require(document.fillMeshHole(hole), "the welded-vertex hole did not fill");
  auto island = *document.getSelectedMeshRingIndices().begin();
  auto vertex = mesh->getPolygon(island).getOrderedVertexIndices().front();

  require(document.deleteMeshSubObjects(
              editor::Settings::MeshSubMode::Vertex, {vertex}) == 1 &&
              mesh->getPolygon(hole).getNumEdges() == 3 &&
              mesh->getPolygon(island).getNumEdges() == 3 &&
              mesh->getPolygon(hole).getEdgeIndexSet() ==
                  mesh->getPolygon(island).getEdgeIndexSet(),
          "deleting a welded vertex did not heal both the hole and island");
}

void drawingContextRejectsEscapesAndSelfCrossings() {
  auto settings = meshDrawSettings();

  editor::Document confined;
  confined.newDoc();
  addMeshWithHole(confined);
  require(confined.armMeshDrawTool(settings), "the confined draw tool did not arm");
  confined.placeMeshDrawVertex({-8.0f, 92.0f}, settings);
  auto fixedBoundary = confined.getMeshDrawContainingRingIndex();
  require(!confined.placeMeshDrawVertex({0.0f, 100.0f}, settings) &&
              confined.getMeshDrawVertices().size() == 1 &&
              confined.getMeshDrawContainingRingIndex() == fixedBoundary &&
              confined.getMeshDrawRejection().find("containing region") != std::string::npos,
          "an out-of-region click was not rejected with clear feedback");

  editor::Document crossing;
  crossing.newDoc();
  require(crossing.armMeshDrawTool(settings), "the crossing draw tool did not arm");
  crossing.placeMeshDrawVertex({0.0f, 0.0f}, settings);
  crossing.placeMeshDrawVertex({100.0f, 100.0f}, settings);
  crossing.placeMeshDrawVertex({0.0f, 100.0f}, settings);
  require(!crossing.placeMeshDrawVertex({100.0f, 0.0f}, settings) &&
              crossing.getMeshDrawVertices().size() == 3 &&
              crossing.getMeshDrawRejection().find("cross itself") != std::string::npos,
          "a self-crossing click was not rejected with clear feedback");

  editor::Document ineligible;
  ineligible.newDoc();
  auto ineligibleMesh = addMesh(ineligible, {0.0f, 0.0f});
  auto* ineligiblePrimitive = ineligible.getWorld()->getPrimitive(ineligibleMesh);
  auto ineligibleCentre = ineligiblePrimitive->getBounds().getCentre();
  auto* layer = ineligible.getWorld()->getActiveLayer();
  layer->setActiveStep(layer->addStep(new bw::core::PrimitiveField()));
  require(ineligible.armMeshDrawTool(settings), "the ineligible-mesh draw tool did not arm");
  require(ineligible.placeMeshDrawVertex(ineligibleCentre, settings) &&
              ineligible.meshDrawCreatesNewPrimitive() &&
              ineligible.getMeshDrawContainingRingIndex() == ~0u,
          "drawing inside an ineligible MeshPrimitive did not start an unconfined MeshPrimitive");

  editor::Document nonMesh;
  nonMesh.newDoc();
  auto rectangle = addRectangle(nonMesh, {0.0f, 0.0f}, 20.0f);
  auto rectangleCentre = nonMesh.getWorld()->getPrimitive(rectangle)->getBounds().getCentre();
  require(nonMesh.armMeshDrawTool(settings), "the non-mesh-context draw tool did not arm");
  require(nonMesh.placeMeshDrawVertex(rectangleCentre, settings) &&
              nonMesh.meshDrawCreatesNewPrimitive() &&
              nonMesh.getMeshDrawContainingRingIndex() == ~0u,
          "drawing inside a non-mesh Primitive did not start an unconfined MeshPrimitive");
}

void theWholeDrawingGestureIsOneUndoEntry() {
  editor::Document document;
  auto settings = meshDrawSettings();
  document.newDoc();
  editor::EditorInteraction interaction;

  auto click = [&](wp::Vector2 const& position) {
    auto input = pointerAt(position);
    input.leftClicked = true;
    interaction.updateSelection(&document, nullptr, settings, input);
  };

  require(document.armMeshDrawTool(settings), "the draw tool did not arm");
  auto const primitivesBefore = document.getWorld()->getNumPrimitives();
  document.setModified(false);
  auto const undoLevelsBefore = editor::getUndoLevels();

  click({0.0f, 0.0f});
  click({100.0f, 0.0f});
  click({100.0f, 100.0f});
  require(editor::getUndoLevels() == undoLevelsBefore && !document.isModified(),
          "placing vertices entered undo history before the shape was closed");

  click({0.0f, 0.0f});
  require(document.getWorld()->getNumPrimitives() == primitivesBefore + 1,
          "closing the shape did not add a Primitive");
  require(editor::getUndoLevels() == undoLevelsBefore + 1,
          "a drawing gesture produced more than one undo entry");
  require(document.isModified(), "closing the shape did not mark the Document modified");

  editor::undo(&document);
  require(editor::getUndoLevels() == undoLevelsBefore &&
              document.getWorld()->getNumPrimitives() == primitivesBefore,
          "one undo did not remove the whole drawn shape");
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

void prefabFieldClickAndKeysAreActiveStepGatedAndDoNotDragPaint() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs;
  auto defineIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Tile");
  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  layer->setActiveStep(fieldIndex);
  editor::EditorInteraction interaction;

  auto click = pointerAt({70.0f, 0.0f});
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);
  require(field->hasSelectedTile() && field->getSelectedTile() == bw::core::Tile{1, 0} &&
              field->getInstance({1, 0}),
          "PrefabField click did not select and place on the Tile");

  auto drag = pointerAt({140.0f, 0.0f});
  drag.leftDown = true;
  drag.leftDragging = true;
  interaction.updateSelection(&document, nullptr, settings, drag);
  require(field->getInstances().size() == 1,
          "dragging painted more Prefab instances after a discrete click");

  field->selectTile({2, 0});
  require(interaction.applyPrefabShortcut(&document, true, false) && field->getInstance({2, 0}),
          "Space routing did not place the selected Prefab");
  require(interaction.applyPrefabShortcut(&document, false, true) && !field->getInstance({2, 0}),
          "Delete routing did not clear the selected Tile");

  layer->setActiveStep(defineIndex);
  require(!interaction.applyPrefabShortcut(&document, true, false),
          "PrefabField shortcut remained active after its step lost focus");
}

void prefabFieldClickPlacesAMeshPrefabPrimitiveWithoutCrashing() {
  editor::Document document;
  editor::Settings settings;
  settings.ghostActive = false;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs;
  auto defineIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Tile");

  layer->setActiveStep(defineIndex);
  definitions->setSelectedPrefab(prefab);
  addMesh(document, {0.0f, 0.0f});
  // Deliberately left selected, mirroring a user who finishes drawing and
  // switches straight to the PrefabField step without deselecting.

  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  layer->setActiveStep(fieldIndex);
  selectPrefabForField(&document, layer, field, prefab);

  editor::EditorInteraction interaction;
  auto click = pointerAt({70.0f, 0.0f});
  click.leftClicked = true;
  interaction.updateSelection(&document, nullptr, settings, click);

  require(field->hasSelectedTile() && field->getInstance({1, 0}),
          "PrefabField click with a Mesh Prefab primitive did not place an instance");
}

void prefabFieldArrowNavigationAndRotationAreActiveStepGated() {
  editor::Document document;
  document.newDoc();
  while (editor::canUndo()) editor::undo(&document);
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs;
  auto defineIndex = layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Tile");
  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  layer->setActiveStep(fieldIndex);
  field->selectTile({0, 0});
  require(field->placeSelected(*layer, {0, 0}), "could not place the rotation fixture");
  editor::EditorInteraction interaction;

  auto const instanceCountBefore = field->getInstances().size();
  auto const rotationBeforeNavigation = field->getInstance({0, 0})->rotation;
  require(interaction.movePrefabTileCursor(&document, -1, 0) &&
              field->getSelectedTile() == bw::core::Tile{-1, 0} &&
              interaction.movePrefabTileCursor(&document, 1, 0) &&
              field->getSelectedTile() == bw::core::Tile{0, 0} &&
              interaction.movePrefabTileCursor(&document, 0, 1) &&
              field->getSelectedTile() == bw::core::Tile{0, 1} &&
              interaction.movePrefabTileCursor(&document, 0, -1) &&
              field->getSelectedTile() == bw::core::Tile{0, 0},
          "arrow navigation did not move the selected Tile cursor one Tile in each direction");
  require(field->getInstances().size() == instanceCountBefore &&
              field->getInstance({0, 0})->rotation == rotationBeforeNavigation,
          "arrow navigation placed, cleared, or rotated a Prefab instance");

  auto const undoBeforeRotation = editor::getUndoLevels();
  require(interaction.rotateSelectedPrefabInstance(&document, false) &&
              field->getInstance({0, 0})->rotation == 3 &&
              editor::getUndoLevels() == undoBeforeRotation + 1,
          "Shift+Left did not wrap to the previous allowed rotation in one undo entry");
  require(interaction.rotateSelectedPrefabInstance(&document, true) &&
              field->getInstance({0, 0})->rotation == 0 &&
              editor::getUndoLevels() == undoBeforeRotation + 2,
          "Shift+Right did not wrap to the next allowed rotation in one undo entry");
  require(interaction.rotateSelectedPrefabInstance(&document, true) &&
              field->getInstance({0, 0})->rotation == 1 &&
              editor::getUndoLevels() == undoBeforeRotation + 3,
          "Shift+Right did not advance through the allowed rotations");

  field->selectTile({99, 99});
  auto const undoBeforeEmptyRotation = editor::getUndoLevels();
  require(interaction.rotateSelectedPrefabInstance(&document, true) &&
              !field->getInstance({99, 99}) &&
              editor::getUndoLevels() == undoBeforeEmptyRotation,
          "rotating an empty Tile changed it or entered undo history");

  auto const selectedBeforeGate = field->getSelectedTile();
  auto const rotationBeforeGate = field->getInstance({0, 0})->rotation;
  layer->setActiveStep(defineIndex);
  require(!interaction.movePrefabTileCursor(&document, 1, 0) &&
              !interaction.rotateSelectedPrefabInstance(&document, true) &&
              field->getSelectedTile() == selectedBeforeGate &&
              field->getInstance({0, 0})->rotation == rotationBeforeGate &&
              editor::getUndoLevels() == undoBeforeEmptyRotation,
          "PrefabField arrow navigation or rotation remained active after its step lost focus");
}

}  // namespace

int main() {
  try {
    plainControlAndShiftClicksApplyTheirSelectionPolicies();
    deletePrimitivesRefusesTheGhostEvenWhenHandedItsIndexDirectly();
    repeatedClicksCycleThroughStackedPrimitives();
    modeAndSubModeChangesAreEditorPreferencesAndClearSelection();
    meshClicksBuildAndSwitchTheActiveProxy();
    rubberBandSelectionSupportsPlainControlAndShiftPolicies();
    meshSubObjectClicksSupportModifiersAndRingCycling();
    draggingASelectedNestedRingDoesNotCycleToItsShell();
    meshRubberBandUsesContainmentAndModifierPolicies();
    meshSelectAllAndBoundsStayScopedToActiveMesh();
    undoRestoresMeshSubObjectSelection();
    meshDragMovesAffectedSubObjectsAsARigidGroup();
    draggingAnOuterRingMovesItsFullNestedHierarchyAsOneGroup();
    meshDragClampsAtLastValidPositionOnSelfIntersection();
    meshDragClampsAtLastValidPositionWhenAHoleWouldEscapeItsOuter();
    addingAMeshHoleInvalidatesTheParentTriangulation();
    meshPolygonCommitRebuildsPrefabFieldInstancesBeforeRegeneration();
    meshDragSnapsToGridBeforeValidating();
    draggingADifferencePrimitiveDoesNotClearItsSelectionOnRelease();
    meshDragCommitIsOneUndoEntryAndUpdatesTheMeshPrimitive();
    vertexDeletionHealsRingAndRefusesAtMinimumCount();
    edgeDeletionWeldsEndpointsAtMidpointAndRefusesAtMinimumCount();
    ringDeletionRemovesJustTheHoleWhenOthersRemain();
    ringDeletionOfTheLastRingDeletesTheMeshPrimitive();
    multiVertexDeleteProcessesAscendingAndReportsActualCount();
    meshSubObjectDeleteIsOneUndoEntry();
    edgeSplitInsertsUnsnappedMidpointAndSelectsBothHalves();
    repeatedEdgeSplitSubdividesIntoFourSegments();
    edgeSplitIsOneUndoEntry();
    drawToolArmsOnlyInVertexSubModeOnAnAcceptingStep();
    drawClicksPlaceGridSnappedVerticesAndRefuseToCloseBelowThree();
    backspaceStepsBackAndEscapeIsTwoStage();
    switchingSubModeOrLeavingMeshModeDisarmsAndDiscards();
    closingADrawnRingCreatesAMeshPrimitiveWithCanonicalWinding();
    drawingContextCreatesHolesAndFilledIslands();
    drawingMultipleHolesKeepsThemOnTheSameComplexPolygon();
    fillingASelectedHoleCreatesASolidAlongsideExistingIslands();
    decomposingAMeshCreatesOrderedRingPrimitives();
    deletingAWeldedVertexHealsTheHoleAndIsland();
    drawingContextRejectsEscapesAndSelfCrossings();
    theWholeDrawingGestureIsOneUndoEntry();
    prefabFieldClickAndKeysAreActiveStepGatedAndDoNotDragPaint();
    prefabFieldClickPlacesAMeshPrefabPrimitiveWithoutCrashing();
    prefabFieldArrowNavigationAndRotationAreActiveStepGated();
    std::cout << "Editor selection interactions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
