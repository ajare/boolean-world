#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <random>
#include <sstream>

#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/TorusPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/LayerBuildStep.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/DefinePrefabs.h>
#include <core/PrimitiveField.h>

#include "Defines.h"
#include "Actions.h"
#include "EditorException.h"
#include "EmbossingCatalogLibrary.h"
#include "ProcMaterialLibrary.h"

namespace editor {
using namespace std;

void setEditorMode(Document* doc, Settings& settings, Settings::Mode mode) {
  if (settings.mode == mode) {
    return;
  }

  // In-progress Mesh tools mean nothing outside the context they started in.
  doc->disarmMeshDrawTool();
  doc->disarmMeshSliceTool();

  if (mode == Settings::Mode::Mesh && doc->isActive()) {
    auto const& selection = doc->getSelectedPrimitiveIndices();
    if (selection.size() == 1 && doc->activateMesh(*selection.begin())) {
      settings.activeMeshPrimitiveIndex = *selection.begin();
    } else {
      doc->clearActiveMesh();
      settings.activeMeshPrimitiveIndex = ~0u;
    }
  } else {
    doc->clearActiveMesh();
    settings.activeMeshPrimitiveIndex = ~0u;
  }

  settings.mode = mode;
  doc->clearSelections();

  // The step filter reads the mode live, and Mesh mode keeps the ghost out of
  // the fold entirely - so which Primitives contribute geometry has just
  // changed and the generator needs telling.
  if (doc->isActive()) {
    doc->getWorld()->getWorldDataGenerator()->refreshPrimitiveFilter();
  }
}

void setMeshSubMode(
    Document* doc, Settings& settings, Settings::MeshSubMode subMode) {
  if (settings.meshSubMode == subMode) {
    return;
  }

  doc->disarmMeshDrawTool();
  doc->disarmMeshSliceTool();
  settings.meshSubMode = subMode;
  doc->clearSelections();
}

bool setWorldName(Document* doc, string const& name) {
  auto world = doc->getWorld();

  world->setName(name);
  return true;
}

bool setWorldBuildVariable(
    Document* doc, string const& name, bw::core::BuildVariableValue value) {
  doc->getWorld()->setBuildVariable(name, move(value));
  return true;
}

bool removeWorldBuildVariable(Document* doc, string const& name) {
  doc->getWorld()->removeBuildVariable(name);
  return true;
}

bool renameWorldBuildVariable(
    Document* doc, string const& oldName, string const& newName) {
  doc->getWorld()->renameBuildVariable(oldName, newName);
  return true;
}

bool setLayerBuildVariable(
    Document*, bw::core::Layer* layer, string const& name,
    bw::core::BuildVariableValue value) {
  layer->setBuildVariable(name, move(value));
  return true;
}

bool removeLayerBuildVariable(
    Document*, bw::core::Layer* layer, string const& name) {
  layer->removeBuildVariable(name);
  return true;
}

bool renameLayerBuildVariable(
    Document*, bw::core::Layer* layer, string const& oldName,
    string const& newName) {
  layer->renameBuildVariable(oldName, newName);
  return true;
}

bool setWorldWedgeGenerationParameters(
    Document* doc,
    bw::core::WedgeGenerationParameters const& parameters) {
  if (!bw::core::WedgeGenerationParametersAreValid(parameters)) {
    return false;
  }
  doc->getWorld()->setWedgeGenerationParameters(parameters);
  return true;
}

bool addLayer(Document* doc, string const& name) {
  auto world = doc->getWorld();

  world->addLayer(name);
  return true;
}

bool setLayerBuildStepEnabled(Document* doc, bw::core::Layer* layer, uint32_t stepIndex, bool enabled) {
  layer->setStepEnabled(stepIndex, enabled);
  return true;
}

bool addLayerBuildStep(
    Document* doc, bw::core::Layer* layer, string const& type) {
  layer->addStep(bw::core::LayerBuildStep::instantiate(type));
  return true;
}

bool removeLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t stepIndex) {
  layer->removeStep(stepIndex);
  return true;
}

bool moveLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t fromIndex, uint32_t toIndex) {
  layer->moveStep(fromIndex, toIndex);
  return true;
}

bool setLayerBuildStepName(
    Document*, bw::core::Layer* layer, uint32_t stepIndex,
    string const& name) {
  layer->getStep(stepIndex)->setName(name);
  // A later RunScript may look this step up by name.
  layer->rebuild();
  return true;
}

bool setTileMapMapSize(
    Document*, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t size) {
  if (layer->getActiveStep() != definitions ||
      definitions->getMapSize() == size) {
    return false;
  }
  definitions->setMapSize(size);
  layer->rebuild();
  return true;
}

bool setTileMapCellSize(
    Document*, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t size) {
  if (layer->getActiveStep() != definitions ||
      definitions->getCellSize() == size) {
    return false;
  }
  definitions->setCellSize(size);
  layer->rebuild();
  return true;
}

bool setNumTileMaps(
    Document*, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t count) {
  if (layer->getActiveStep() != definitions ||
      definitions->getNumTileMaps() == count) {
    return false;
  }
  definitions->setNumTileMaps(count);
  layer->rebuild();
  return true;
}

bool toggleTileMapCell(
    Document*, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, bw::core::TileMap* tileMap,
    uint32_t x, uint32_t y) {
  if (layer->getActiveStep() != definitions || !definitions->isEnabled() ||
      definitions->getTileMap(tileMap->getIndex()) != tileMap) {
    return false;
  }
  tileMap->toggleCell(x, y);
  layer->rebuild();
  return true;
}

bool movePrimitiveToLayerBuildStep(
    Document* doc,
    bw::core::Layer* layer,
    bw::core::Primitive* primitive,
    uint32_t targetStepIndex) {
  layer->movePrimitiveToStep(primitive, targetStepIndex);

  // The rebuild re-stamps every derived index, so the selection this action
  // was invoked from now names a different Primitive. Follow the one that
  // moved rather than leaving the Edit Primitive view pointing elsewhere.
  // A Primitive's id is its index in its own Layer's derived collection, so
  // it is only a valid selection while the rebuild still emits it.
  auto movedIndex = primitive->getId();
  if (movedIndex < layer->getNumPrimitives() &&
      layer->getPrimitive(movedIndex) == primitive) {
    doc->setSelectedPrimitiveIndices({movedIndex});
  } else {
    doc->clearSelections();
  }

  return true;
}

namespace {

void rebuildPrefabAuthoringContext(Document* doc, bw::core::Layer* layer) {
  doc->disarmMeshDrawTool();
  doc->clearActiveMesh();
  doc->clearSelections();
  layer->rebuild();

  // The fold filter reads the selected Prefab live (Document.cpp), so which
  // Primitives it admits has just changed. Generate this small, isolated
  // authoring fold synchronously: leaving the previous Prefab's committed
  // fold in place while an asynchronous replacement runs renders its outline
  // behind the newly selected Prefab.
  if (doc->isActive()) {
    auto* generator = doc->getWorld()->getWorldDataGenerator();
    if (auto* dynamicGenerator =
            dynamic_cast<bw::core::DynamicWorldDataGenerator*>(generator)) {
      dynamicGenerator->generateBlocking();
    } else {
      generator->refreshPrimitiveFilter();
    }
  }
}

}  // namespace

bool selectPrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab) {
  step->setSelectedPrefab(prefab);
  rebuildPrefabAuthoringContext(doc, layer);
  return false;
}

bool createPrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step) {
  uint32_t suffix = 1;
  string name;
  do {
    name = format("Prefab {}", suffix++);
  } while (any_of(
      step->getPrefabs().begin(), step->getPrefabs().end(),
      [&name](auto const* prefab) { return prefab->getName() == name; }));

  step->setSelectedPrefab(step->addPrefab(name));
  rebuildPrefabAuthoringContext(doc, layer);
  return true;
}

bool renamePrefab(
    Document*, bw::core::Layer*, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, string const& name) {
  step->setPrefabName(prefab, name);
  return true;
}

string prefabDeletionBlockedReason(
    bw::core::Layer const* layer, bw::core::DefinePrefabs const* step,
    bw::core::Prefab const* prefab) {
  for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
    auto const* field = dynamic_cast<bw::core::PrefabField const*>(layer->getStep(i));
    if (field && field->getDefinePrefabsStepId() == step->getId() &&
        field->referencesPrefab(prefab->getId())) {
      return "Cannot delete a Prefab referenced by a PrefabField";
    }
  }
  return "";
}

bool deletePrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab) {
  auto reason = prefabDeletionBlockedReason(layer, step, prefab);
  if (!reason.empty()) {
    throw bw::core::CoreException(reason);
  }
  step->removePrefab(prefab);
  rebuildPrefabAuthoringContext(doc, layer);
  return true;
}

bool setPrefabTilingType(
    Document*, bw::core::Layer*, bw::core::DefinePrefabs* step,
    bw::core::PrefabTilingType type) {
  step->setTilingType(type);
  return true;
}

string prefabSizeChangeBlockedReason(
    bw::core::Layer const* layer, bw::core::DefinePrefabs const* step,
    bw::core::Prefab const* prefab, bw::core::PrefabTileSize size) {
  auto const oldSize = prefab->getTileSize();
  for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
    auto const* field = dynamic_cast<bw::core::PrefabField const*>(layer->getStep(i));
    if (field && field->getDefinePrefabsStepId() == step->getId() &&
        !field->canMigratePrefabSize(prefab->getId(), oldSize, size)) {
      return "Changing size would collide with an occupied destination Tile";
    }
  }
  return "";
}

bool setPrefabTileSize(
    Document*, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, bw::core::PrefabTileSize size) {
  auto reason = prefabSizeChangeBlockedReason(layer, step, prefab, size);
  if (!reason.empty()) throw bw::core::CoreException(reason);
  auto const oldSize = prefab->getTileSize();
  if (oldSize == size) return false;
  for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
    auto* field = dynamic_cast<bw::core::PrefabField*>(layer->getStep(i));
    if (field && field->getDefinePrefabsStepId() == step->getId()) {
      field->migratePrefabSize(prefab->getId(), oldSize, size);
    }
  }
  step->setPrefabTileSize(prefab, size);
  layer->rebuild();
  return true;
}

bool setPrefabTags(
    Document*, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, set<string> const& tags) {
  auto const oldTags = prefab->getTags();
  step->setPrefabTags(prefab, tags);
  if (prefab->getTags() == oldTags) return false;
  layer->rebuild();
  return true;
}

bool bindPrefabField(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::DefinePrefabs* definitions) {
  field->bind(*layer, definitions);
  layer->rebuild();
  return true;
}

bool selectPrefabForField(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Prefab* prefab) {
  auto* definitions = field->getDefinePrefabs(*layer);
  if (!definitions) return false;
  field->setSelectedPrefab(*definitions, prefab);
  return false;
}

bool placePrefabInstance(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile) {
  if (layer->getActiveStep() != field) return false;
  return field->placeSelected(*layer, tile);
}

bool placePrefabInstanceWithMode(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bw::core::TileMode mode) {
  if (layer->getActiveStep() != field) return false;
  return field->placeSelected(*layer, tile, mode);
}

bool clearPrefabInstance(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile) {
  if (layer->getActiveStep() != field) return false;
  return field->clearInstance(*layer, tile);
}

bool rotatePrefabInstance(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bool next) {
  if (layer->getActiveStep() != field) return false;
  return field->rotateInstance(*layer, tile, next);
}

bool setPrefabInstanceMode(
    Document*, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bw::core::TileMode mode) {
  if (layer->getActiveStep() != field) return false;
  return field->setInstanceMode(*layer, tile, mode);
}

bool setWorldDescription(Document* doc, string const& desc) {
  auto world = doc->getWorld();

  world->setDescription(desc);
  return true;
}

bool setPlayerStartPosition(Document* doc, wp::Vector2 const& pos) {
  auto world = doc->getWorld();

  world->setPlayerStartPosition(pos);
  return true;
}

bool setPlayerStartAngle(Document* doc, float angle) {
  auto world = doc->getWorld();

  world->setPlayerStartAngle(angle);
  return true;
}

bool selectWorldVertex(Document* doc, uint32_t worldVertexIndex) {
  doc->setSelectedWorldVertexIndex(worldVertexIndex);
  return false;
}

bool selectTriggerLine(Document* doc, uint32_t triggerLineIndex) {
  doc->setSelectedTriggerLineIndex(triggerLineIndex);
  return false;
}

bool deleteTriggerLine(Document* doc, uint32_t triggerLineIndex) {
  doc->getWorld()->removeTriggerLine(triggerLineIndex);
  return true;
}

bool setTriggerLineSide(Document* doc, bw::core::WorldTriggerLine* triggerLine, bw::core::WorldTriggerLineSide side) {
  triggerLine->setSide(side);
  return true;
}

bool selectPrimitive(Document* doc, uint32_t primitiveIndex) {
  doc->setSelectedPrimitiveIndices({primitiveIndex});
  return false;
}

bool togglePrimitiveSelected(Document* doc, uint32_t primitiveIndex) {
  if (doc->indexInSelection(primitiveIndex)) {
    doc->removeSelectedPrimitiveIndex(primitiveIndex);
  } else {
    doc->addSelectedPrimitiveIndex(primitiveIndex);
  }

  return false;
}

bool selectPrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  doc->setSelectedPrimitiveIndices(primitiveIndices);
  return false;
}

bool addPrimitivesToSelection(Document* doc, set<uint32_t> const& primitiveIndices) {
  doc->addSelectedPrimitiveIndices(primitiveIndices);
  return false;
}

bool togglePrimitivesSelected(Document* doc, set<uint32_t> const& primitiveIndices) {
  for (auto primitiveIndex : primitiveIndices) {
    if (doc->indexInSelection(primitiveIndex)) {
      doc->removeSelectedPrimitiveIndex(primitiveIndex);
    } else {
      doc->addSelectedPrimitiveIndex(primitiveIndex);
    }
  }

  return false;
}

bool clearSelections(Document* doc) {
  doc->clearSelections();
  return false;
}

bool selectMeshSubObjects(
    Document* doc, Settings::MeshSubMode subMode,
    set<uint32_t> const& indices) {
  doc->setSelectedMeshSubObjectIndices(subMode, indices);
  return false;
}

bool addMeshSubObjectsToSelection(
    Document* doc, Settings::MeshSubMode subMode,
    set<uint32_t> const& indices) {
  doc->addSelectedMeshSubObjectIndices(subMode, indices);
  return false;
}

bool toggleMeshSubObjectsSelected(
    Document* doc, Settings::MeshSubMode subMode,
    set<uint32_t> const& indices) {
  doc->toggleSelectedMeshSubObjectIndices(subMode, indices);
  return false;
}

bool selectAllMeshSubObjects(Document* doc, Settings::MeshSubMode subMode) {
  if (!doc->getActiveMesh()) {
    return false;
  }
  doc->setSelectedMeshSubObjectIndices(
      subMode, doc->getSelectableMeshSubObjectIndices(subMode));
  return false;
}

bool setMeshVertexPosition(Document* doc, uint32_t vertexIndex, wp::Vector2 const& position) {
  return doc->moveMeshVertexTo(vertexIndex, position);
}

bool setMeshVertexMetadata(
    Document* doc, uint32_t vertexIndex,
    map<string, string> const& metadata) {
  return doc->setActiveMeshVertexMetadata(vertexIndex, metadata);
}

bool setMeshEdgeMetadata(
    Document* doc, uint32_t edgeIndex,
    map<string, string> const& metadata) {
  return doc->setActiveMeshEdgeMetadata(edgeIndex, metadata);
}

bool setMeshEdgeCollisionOverride(
    Document* doc, uint32_t edgeIndex, optional<bool> collides) {
  return doc->setActiveMeshEdgeCollisionOverride(edgeIndex, collides);
}

bool setMeshEdgeVisible(Document* doc, uint32_t edgeIndex, bool visible) {
  return doc->setActiveMeshEdgeVisible(edgeIndex, visible);
}

bool setMeshEdgeNormalMapOverride(
    Document* doc, uint32_t edgeIndex,
    bw::core::WallNormalMapOverride const& overrideValue) {
  return doc->setActiveMeshEdgeNormalMapOverride(edgeIndex, overrideValue);
}

bool setMeshEdgeWallMaskOverride(
    Document* doc, uint32_t edgeIndex,
    bw::core::WallMaskOverride const& overrideValue) {
  return doc->setActiveMeshEdgeWallMaskOverride(edgeIndex, overrideValue);
}

bool deleteMeshSubObjects(
    Document* doc, Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  return doc->deleteMeshSubObjects(subMode, indices) > 0;
}

bool splitMeshEdges(Document* doc, set<uint32_t> const& edgeIndices) {
  return doc->splitMeshEdges(edgeIndices) > 0;
}

bool sliceMesh(Document* doc, uint32_t secondVertexIndex) {
  return doc->completeMeshSlice(secondVertexIndex);
}

bool fillMeshHole(Document* doc, uint32_t holeRingIndex) {
  return doc->fillMeshHole(holeRingIndex);
}

bool recentreActiveMesh(Document* doc) {
  return doc->recentreActiveMesh();
}

bool createMeshPrimitiveFromDrawnRing(Document* doc) {
  auto createsNewPrimitive = doc->meshDrawCreatesNewPrimitive();
  auto* mesh = doc->closeMeshDrawRing();
  if (!mesh) {
    return false;
  }

  if (createsNewPrimitive) {
    setPrimitiveDefaultMaterials(mesh);
  }
  return true;
}

bool createPrimitiveFromGhost(Document* doc) {
  auto world = doc->getWorld();
  auto* activeStep = world->getActiveLayer()->getActiveStep();
  if (!activeStep->acceptsNewPrimitives()) {
    return false;
  }

  auto ghost = doc->getGhost();
  auto prim = ghost->copy();

  // Clear the editor-only ghost flag.
  prim->setFlags(prim->getFlags() & ~BW_PRIMITIVE_GHOST_FLAG);

  // copy() carries mWorld over from ghost (already added to world), so prim
  // must be added to a Layer before anything on it can trigger
  // notifyWorldChanged - otherwise World::primitiveChanged's scan for its
  // owning Layer finds none and throws.
  world->addPrimitive(prim);

  // Set material defaults
  setPrimitiveDefaultMaterials(prim);

  return true;
}

bool beginClonePlacement(Document* doc, set<uint32_t> const& primitiveIndices) {
  if (!doc->isActive() || doc->clonePlacementArmed() || primitiveIndices.empty()) {
    return false;
  }

  auto world = doc->getWorld();
  if (!world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  // Resolve every source before adding anything: the indices name positions
  // in the World's Primitive list, and this is about to grow it.
  vector<bw::core::Primitive*> sources;
  for (auto index : primitiveIndices) {
    if (index >= world->getNumPrimitives()) continue;
    if (auto* primitive = world->getPrimitive(index)) {
      sources.push_back(primitive);
    }
  }
  if (sources.empty()) {
    return false;
  }

  // The snapshot has to be taken before the clones exist: this action is
  // committed at the far end of the placement gesture, and undoing it must
  // return to a World without them in it.
  beginTransaction(
      doc, CommandId::BeginClonePlacement,
      numeric_limits<float>::quiet_NaN(),
      format("Clone {} Primitive(s)", sources.size()));

  set<uint32_t> cloneIndices;
  for (auto* source : sources) {
    cloneIndices.insert(world->addPrimitive(source->copy()));
  }

  if (!doc->armClonePlacement(cloneIndices)) {
    cancelUndoableAction(doc);
    return false;
  }

  // The clones, not their sources, are what the rest of the gesture - and
  // whatever the user does after placing them - acts on.
  doc->setSelectedPrimitiveIndices(cloneIndices);
  return true;
}

void commitClonePlacement(Document* doc) {
  if (!doc->clonePlacementArmed()) {
    return;
  }

  doc->disarmClonePlacement();
  if (undoableActionInProgress()) {
    commitUndoableAction(doc);
  }
}

void cancelClonePlacement(Document* doc) {
  if (!doc->clonePlacementArmed()) {
    return;
  }

  auto cloneIndices = doc->getClonePlacementPrimitiveIndices();
  doc->disarmClonePlacement();

  if (undoableActionInProgress()) {
    // The ordinary path: the World goes back to the snapshot taken before
    // the clones were made, and the history never hears about any of it.
    cancelUndoableAction(doc);
    return;
  }

  // Something else committed our transaction mid-gesture (an edit made from
  // a panel while the clones were in flight), so they are already in the
  // history. Removing them is then an ordinary undoable action of its own.
  transactUndoableActionAtomically(doc, CommandId::DeletePrimitives, [&](Document* actionDoc) { return deletePrimitives(actionDoc, cloneIndices); });
}

bool decomposeMeshPrimitive(Document* doc, uint32_t primitiveIndex) {
  auto world = doc->getWorld();
  auto* source = dynamic_cast<bw::core::MeshPrimitive*>(
      world->getPrimitive(primitiveIndex));
  if (!source ||
      !world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  auto created = source->decomposeFilledRegions();
  if (created.empty()) return false;

  // The source's transform children must survive its removal. Capture them
  // before adding copies, whose inherited parent is intentionally preserved.
  vector<bw::core::Primitive*> children;
  for (auto* primitive : world->getActiveLayer()->getPrimitives()) {
    if (primitive->getParent() == source) children.push_back(primitive);
  }

  for (auto* part : created) world->addPrimitive(part);
  for (auto* child : children) child->setParent(created.front());

  doc->clearActiveMesh();
  world->removePrimitives({primitiveIndex});
  set<uint32_t> selected;
  for (uint32_t index = 0; index < world->getNumPrimitives(); ++index) {
    if (find(created.begin(), created.end(), world->getPrimitive(index)) !=
        created.end()) {
      selected.insert(index);
    }
  }
  doc->setSelectedPrimitiveIndices(selected);
  return true;
}

bool cloneRotatedPrimitive(Document* doc, uint32_t primitiveIndex, float angle) {
  auto world = doc->getWorld();
  if (!world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  auto primitive = world->getPrimitive(primitiveIndex);

  world->addPrimitive(primitive->rotatedCopy(angle));
  return true;
}

bool deletePrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  // The ghost must never be deletable, even if some future selection path
  // manages to hand its index in here directly.
  vector<uint32_t> vec;
  for (auto index : primitiveIndices) {
    if (index != uint32_t(ED_GHOST_INDEX)) {
      vec.push_back(index);
    }
  }
  doc->getWorld()->removePrimitives(vec);
  return true;
}

bool bakePrimitives(Document* doc, set<uint32_t> const& primitiveIndices) {
  vector<uint32_t> vec(primitiveIndices.begin(), primitiveIndices.end());
  auto index = doc->getWorld()->convertPrimitivesToMesh(vec);

  if (index != ~0u) {
    doc->setSelectedPrimitiveIndices({index});
    return true;
  } else {
    return false;
  }
}

bool clipPrimitivesToGrid(Document* doc, set<uint32_t> const& primitiveIndices, float gridSize) {
  vector<uint32_t> vec(primitiveIndices.begin(), primitiveIndices.end());

  auto world = doc->getWorld();

  // Create a mesh primitive to clip
  auto meshTemplateIndex = world->convertPrimitivesToMesh(vec);

  if (meshTemplateIndex == ~0u) {
    return false;
  }

  auto meshTemplate = world->getPrimitive(meshTemplateIndex);

  // Get all grid cells
  auto templateBounds = meshTemplate->getBounds();
  templateBounds.expandToGrid(wp::Vector2(gridSize, gridSize));

  wp::Vector2 minExtent, maxExtent;
  templateBounds.getExtents(minExtent, maxExtent);

  int dx = (int)((maxExtent.x - minExtent.x) / gridSize);
  int dy = (int)((maxExtent.y - minExtent.y) / gridSize);

  vector<bw::core::Primitive*> createdPrimitives;

  for (int y = 0; y < dy; y++) {
    for (int x = 0; x < dx; x++) {
      wp::Vector2 cellMin{minExtent.x + x * gridSize, minExtent.y + y * gridSize};
      wp::Vector2 cellMax = cellMin + gridSize;
      bw::core::RectanglePolygon cell(
          bw::core::Primitive::Operation::Intersection,
          bw::core::Primitive::FillRule::EvenOdd,
          1.0f);
      cell.setPosition((cellMin + cellMax) * 0.5f);
      cell.setSize(gridSize, gridSize);
      cell.setPriority(BW_PRIORITY_MAX_VALUE);
      cell.updateVertexPositions();

      auto cellMesh = world->createMeshPrimitive({meshTemplate, &cell});
      if (cellMesh) {
        createdPrimitives.push_back(cellMesh);
      }
    }
  }

  // Delete temporary mesh primitive
  world->removePrimitive(meshTemplateIndex);

  for (auto p : createdPrimitives) {
    world->addPrimitive(p);
  }

  return true;
}

bool setPrimitiveOperation(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::Operation op) {
  primitive->setOperation(op);
  return true;
}

bool setPrimitiveFillRule(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::FillRule fillRule) {
  primitive->setFillRule(fillRule);
  return true;
}

bool setPrimitiveOrientation(Document* doc, bw::core::Primitive* primitive, float orient) {
  primitive->setOrientation(orient);
  return true;
}

bool setPrimitiveSize(Document* doc, bw::core::Primitive* primitive, float size) {
  primitive->setSize(size, size);
  return true;
}

bool setPrimitivePosition(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& position) {
  primitive->setPosition(position);

  // Update vertices for visual purposes
  primitive->updateVertexPositions();
  return true;
}

bool setPrimitiveTransformOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& transformOrigin) {
  primitive->setTransformOffset(transformOrigin);

  // Update vertices for visual purposes
  primitive->updateVertexPositions();
  return true;
}

bool setPrimitiveInfluenceOriginOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& influenceOriginOffset) {
  primitive->setInfluenceEyeOriginOffset(influenceOriginOffset);
  return true;
}

bool setPrimitiveFollowOrbitAngle(Document* doc, bw::core::Primitive* primitive, bool orient) {
  primitive->setFollowOrbitAngle(orient);
  return true;
}

bool setPrimitivePriority(
    Document*, bw::core::Primitive* primitive, uint8_t priority) {
  primitive->setPriority(priority);
  return true;
}

namespace {

string newAudioEmitterGuid() {
  static random_device source;
  static mt19937_64 random(source());

  auto const high = random();
  auto const low = random();
  return format(
      "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
      static_cast<uint32_t>(high >> 32),
      static_cast<uint16_t>(high >> 16),
      static_cast<uint16_t>((high & 0x0fffull) | 0x4000ull),
      static_cast<uint16_t>(((low >> 48) & 0x3fffull) | 0x8000ull),
      low & 0x0000ffffffffffffull);
}

template <typename Change>
bool changePrimitiveAudioEmitter(
    bw::core::Primitive* primitive, uint32_t emitterIndex, Change change) {
  auto emitters = primitive->getAudioEmitters();
  if (emitterIndex >= emitters.size()) {
    return false;
  }
  change(emitters[emitterIndex]);
  primitive->setAudioEmitters(emitters);
  return true;
}

}  // namespace

bool addPrimitiveAudioEmitter(Document*, bw::core::Primitive* primitive) {
  auto emitters = primitive->getAudioEmitters();
  bw::core::AudioEmitter emitter;
  emitter.guid = newAudioEmitterGuid();
  emitters.push_back(move(emitter));
  primitive->setAudioEmitters(emitters);
  return true;
}

bool setPrimitiveAudioEmitterOffset(
    Document*, bw::core::Primitive* primitive, uint32_t emitterIndex,
    wp::Vector2 const& offset) {
  return changePrimitiveAudioEmitter(
      primitive, emitterIndex,
      [&offset](auto& emitter) { emitter.offset = offset; });
}

bool setPrimitiveAudioEmitterHeightOffset(
    Document*, bw::core::Primitive* primitive, uint32_t emitterIndex,
    float heightOffset) {
  return changePrimitiveAudioEmitter(
      primitive, emitterIndex,
      [heightOffset](auto& emitter) { emitter.heightOffset = heightOffset; });
}

bool setPrimitiveAudioEmitterSoundId(
    Document*, bw::core::Primitive* primitive, uint32_t emitterIndex,
    string const& soundId) {
  return changePrimitiveAudioEmitter(
      primitive, emitterIndex,
      [&soundId](auto& emitter) { emitter.soundId = soundId; });
}

bool deletePrimitiveAudioEmitter(
    Document*, bw::core::Primitive* primitive, uint32_t emitterIndex) {
  auto emitters = primitive->getAudioEmitters();
  if (emitterIndex >= emitters.size()) {
    return false;
  }
  emitters.erase(emitters.begin() + emitterIndex);
  primitive->setAudioEmitters(emitters);
  return true;
}

bool setPrimitiveSurfaceMaterial(
    Document*, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface,
    bw::core::SurfaceMaterialReference const& material) {
  auto properties = primitive->getProperties();
  switch (surface) {
    case PrimitiveMaterialSurface::Floor:
      properties.floorMaterial = material;
      break;
    case PrimitiveMaterialSurface::Ceiling:
      properties.ceilingMaterial = material;
      break;
    case PrimitiveMaterialSurface::Wall:
      properties.wallMaterial = material;
      break;
  }
  primitive->setProperties(properties);
  return true;
}

bool setPrimitiveSubMaterial(
    Document* doc, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface, string const& subMaterialId) {
  return setPrimitiveSurfaceMaterial(
      doc, primitive, surface,
      bw::core::SurfaceMaterialReference::subMaterial(subMaterialId));
}

bool setPrimitiveTriplanarMaterial(
    Document* doc, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface, string const& qualifiedResourceName) {
  return setPrimitiveSurfaceMaterial(
      doc, primitive, surface,
      bw::core::SurfaceMaterialReference::triplanar(qualifiedResourceName));
}

bool setPrimitiveEmbossPreset(
    Document*, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface, string const& embossPresetId) {
  auto properties = primitive->getProperties();
  switch (surface) {
    case PrimitiveMaterialSurface::Floor:
      properties.floorEmbossPresetId = embossPresetId;
      break;
    case PrimitiveMaterialSurface::Ceiling:
      properties.ceilingEmbossPresetId = embossPresetId;
      break;
    case PrimitiveMaterialSurface::Wall:
      properties.wallEmbossPresetId = embossPresetId;
      break;
  }
  primitive->setProperties(properties);
  return true;
}

bw::core::PrimitivePropertySet movedSurfaceZ(
    bw::core::PrimitivePropertySet properties,
    PrimitiveMaterialSurface surface, float delta) {
  switch (surface) {
    case PrimitiveMaterialSurface::Floor:
      properties.floorZ.baseElevation = std::min(
          properties.floorZ.baseElevation + delta,
          properties.ceilingZ.baseElevation);
      break;
    case PrimitiveMaterialSurface::Ceiling:
      properties.ceilingZ.baseElevation = std::max(
          properties.ceilingZ.baseElevation + delta,
          properties.floorZ.baseElevation);
      break;
    case PrimitiveMaterialSurface::Wall:
      break;
  }
  return properties;
}

bw::core::PrimitivePropertySet movedLiquidLevel(
    bw::core::PrimitivePropertySet properties,
    PrimitiveMaterialSurface surface, float delta) {
  if (surface == PrimitiveMaterialSurface::Floor) {
    properties.liquidLevel = std::max(0.0f, properties.liquidLevel + delta);
  }
  return properties;
}

optional<ElevationSpanEnd> elevationSpanEndTowardsView(
    bw::core::Primitive const& primitive, PrimitiveMaterialSurface surface,
    wp::Vector2 const& viewDirection) {
  auto const& properties = primitive.getProperties();
  bw::core::ElevationSpan const* span{};
  switch (surface) {
    case PrimitiveMaterialSurface::Floor:
      span = &properties.floorSpan;
      break;
    case PrimitiveMaterialSurface::Ceiling:
      span = &properties.ceilingSpan;
      break;
    case PrimitiveMaterialSurface::Wall:
      return nullopt;
  }

  auto const bounds = primitive.getElevationBounds(span->directionAngle);
  auto const lower = primitive.transformLocalPointToWorld(
      (bounds.corners[0] + bounds.corners[1]) * 0.5f);
  auto const upper = primitive.transformLocalPointToWorld(
      (bounds.corners[2] + bounds.corners[3]) * 0.5f);
  auto const alignment = (upper - lower).dot(viewDirection);
  constexpr float directionEpsilon = 1.0e-6f;
  if (std::abs(alignment) <= directionEpsilon) {
    return nullopt;
  }
  return alignment > 0.0f ? ElevationSpanEnd::Upper
                          : ElevationSpanEnd::Lower;
}

bw::core::PrimitivePropertySet movedElevationSpanEnd(
    bw::core::Primitive const& primitive, PrimitiveMaterialSurface surface,
    ElevationSpanEnd end, float delta) {
  auto current = primitive.getProperties();
  if (surface == PrimitiveMaterialSurface::Wall || delta == 0.0f) {
    return current;
  }

  auto requested = current;
  auto& span = surface == PrimitiveMaterialSurface::Floor
                   ? requested.floorSpan
                   : requested.ceilingSpan;
  auto& elevation = end == ElevationSpanEnd::Lower
                        ? span.lowerElevation
                        : span.upperElevation;
  auto const originalElevation = elevation;
  elevation += delta;

  auto planeForSpan = [&](bw::core::ElevationSpan const& value) {
    auto bounds = primitive.getElevationBounds(value.directionAngle);
    auto run = bounds.maximumDirection - bounds.minimumDirection;
    if (!(run > 0.0f)) {
      return bw::core::Elevation{value.lowerElevation};
    }
    auto gradient =
        bounds.direction *
        ((value.upperElevation - value.lowerElevation) / run);
    return bw::core::Elevation{
        value.lowerElevation -
            gradient.dot(bounds.direction * bounds.minimumDirection),
        gradient};
  };

  auto const currentFloor = primitive.getElevationPlane(
      bw::core::PrimitiveSurface::Floor);
  auto const currentCeiling = primitive.getElevationPlane(
      bw::core::PrimitiveSurface::Ceiling);
  auto const requestedFloor = planeForSpan(requested.floorSpan);
  auto const requestedCeiling = planeForSpan(requested.ceilingSpan);

  // An affine gap reaches its minimum at a corner. The Primitive's local
  // axis-aligned fitted bounds contain all of its Rings, so checking those
  // corners guarantees the edited floor and ceiling do not cross inside it.
  auto const checkBounds = primitive.getElevationBounds(0.0f);
  float acceptedFraction = 1.0f;
  for (auto const& point : checkBounds.corners) {
    auto currentGap =
        currentCeiling.evaluate(point) - currentFloor.evaluate(point);
    auto requestedGap =
        requestedCeiling.evaluate(point) - requestedFloor.evaluate(point);
    if (requestedGap < 0.0f) {
      if (!(currentGap > 0.0f)) {
        acceptedFraction = 0.0f;
      } else {
        acceptedFraction = std::min(
            acceptedFraction, currentGap / (currentGap - requestedGap));
      }
    }
  }

  elevation = originalElevation + delta * acceptedFraction;
  return requested;
}

bool setPrimitiveProperties(
    Document*, bw::core::Primitive* primitive,
    bw::core::PrimitivePropertySet const& properties) {
  primitive->setProperties(properties);
  return true;
}

bool createSubMaterial(
    Document*, ProcMaterialLibrary* library,
    string const& resourceName, string const& displayName,
    uint32_t materialIndex, vector<float> const& paramValues,
    array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip, string* createdId) {
  auto id = library->createSubMaterial(
      resourceName, displayName, materialIndex, paramValues, baseColour,
      chip);
  if (createdId) *createdId = move(id);
  return true;
}

bool renameSubMaterial(
    Document*, ProcMaterialLibrary* library,
    string const& subMaterialId, string const& displayName) {
  library->renameSubMaterial(subMaterialId, displayName);
  return true;
}

bool editSubMaterial(
    Document*, ProcMaterialLibrary* library,
    string const& subMaterialId, vector<float> const& paramValues,
    array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip) {
  library->editSubMaterial(
      subMaterialId, paramValues, baseColour, chip);
  return true;
}

string subMaterialDeletionBlockedReason(Document* doc, string const& subMaterialId) {
  if (!doc || !doc->isActive()) return {};
  ostringstream report;
  uint32_t index{0};
  for (auto const* primitive : doc->getWorld()->getPrimitives()) {
    auto const& properties = primitive->getProperties();
    vector<string> surfaces;
    auto referencesSubMaterial = [&](auto const& material) {
      return material.kind == bw::core::SurfaceMaterialKind::SubMaterial &&
             material.reference == subMaterialId;
    };
    if (referencesSubMaterial(properties.floorMaterial))
      surfaces.push_back("floor");
    if (referencesSubMaterial(properties.ceilingMaterial))
      surfaces.push_back("ceiling");
    if (referencesSubMaterial(properties.wallMaterial))
      surfaces.push_back("wall");
    if (!surfaces.empty()) {
      if (report.tellp() > 0) report << "; ";
      report << "Primitive " << index << " (";
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (i) report << ", ";
        report << surfaces[i];
      }
      report << ')';
    }
    ++index;
  }
  if (report.tellp() == 0) return {};
  return "Sub-material '" + subMaterialId + "' is still referenced by " + report.str();
}

bool deleteSubMaterial(
    Document* doc, ProcMaterialLibrary* library,
    string const& subMaterialId, string* blockedReason) {
  auto reason = subMaterialDeletionBlockedReason(doc, subMaterialId);
  if (blockedReason) *blockedReason = reason;
  if (!reason.empty()) return false;
  library->deleteSubMaterial(subMaterialId);
  return true;
}

bool createEmbossPreset(
    Document*, EmbossingCatalogLibrary* library,
    string const& displayName, bw::core::EmbossData const& emboss,
    string* createdId) {
  auto id = library->createPreset(displayName, emboss);
  if (createdId) *createdId = move(id);
  return true;
}

bool renameEmbossPreset(
    Document*, EmbossingCatalogLibrary* library,
    string const& presetId, string const& displayName) {
  library->renamePreset(presetId, displayName);
  return true;
}

bool editEmbossPreset(
    Document*, EmbossingCatalogLibrary* library,
    string const& presetId, bw::core::EmbossData const& emboss) {
  library->editPreset(presetId, emboss);
  return true;
}

namespace {

void reportEmbossReferences(
    ostringstream& report, bw::core::Primitive const* primitive,
    string const& location, string const& presetId) {
  auto const& properties = primitive->getProperties();
  vector<string> surfaces;
  if (properties.floorEmbossPresetId == presetId) surfaces.push_back("floor");
  if (properties.ceilingEmbossPresetId == presetId) surfaces.push_back("ceiling");
  if (properties.wallEmbossPresetId == presetId) surfaces.push_back("wall");
  if (surfaces.empty()) return;
  if (report.tellp() > 0) report << "; ";
  report << location << " (";
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (i) report << ", ";
    report << surfaces[i];
  }
  report << ')';
}

}  // namespace

string embossPresetDeletionBlockedReason(Document* doc, string const& presetId) {
  if (!doc || !doc->isActive()) return {};
  ostringstream report;
  for (auto const* layer : doc->getWorld()->getLayers()) {
    for (uint32_t stepIndex = 0; stepIndex < layer->getNumSteps(); ++stepIndex) {
      auto const* step = layer->getStep(stepIndex);
      if (auto const* field = dynamic_cast<bw::core::PrimitiveField const*>(step)) {
        for (uint32_t i = 0; i < field->getNumPrimitives(); ++i) {
          reportEmbossReferences(
              report, field->getPrimitive(i),
              "Layer " + to_string(layer->getId()) + ", step " +
                  to_string(stepIndex) + ", Primitive " + to_string(i),
              presetId);
        }
      } else if (auto const* definitions =
                     dynamic_cast<bw::core::DefinePrefabs const*>(step)) {
        for (auto const* prefab : definitions->getPrefabs()) {
          for (uint32_t i = 0; i < prefab->getNumPrimitives(); ++i) {
            reportEmbossReferences(
                report, prefab->getPrimitive(i),
                "Layer " + to_string(layer->getId()) + ", step " +
                    to_string(stepIndex) + ", Prefab '" + prefab->getName() +
                    "', Primitive " + to_string(i),
                presetId);
          }
        }
      }
    }
  }
  if (report.tellp() == 0) return {};
  return "Emboss preset '" + presetId + "' is still referenced by " + report.str();
}

bool deleteEmbossPreset(
    Document* doc, EmbossingCatalogLibrary* library,
    string const& presetId, string* blockedReason) {
  auto reason = embossPresetDeletionBlockedReason(doc, presetId);
  if (blockedReason) *blockedReason = reason;
  if (!reason.empty()) return false;
  library->deletePreset(presetId);
  return true;
}

bool increasePrimitivePriority(Document*, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = min(BW_PRIORITY_MAX_VALUE, priority + 1);

  primitive->setPriority((uint8_t)newPriority);
  return true;
}

bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = max(0, priority - 1);

  primitive->setPriority((uint8_t)newPriority);
  return true;
}

bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value) {
  primitive->updateAnimatedPropertyEvent(key, index, eventType, triggerType, value);
  return true;
}

bool addPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value) {
  primitive->addAnimatedPropertyEvent(key, eventType, triggerType, value);
  return true;
}

bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  primitive->removeAnimatedPropertyEvent(key, index);
  return true;
}

bool setPrimitiveCaptureMode(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, bw::core::ValueCaptureMode mode) {
  primitive->setCaptureMode(key, mode);
  return true;
}

bool addKeyToInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).addPoint(time, value);
    } else {
      mutation.influence(key).addPoint(time, value);
    }
  }

  return true;
}

bool removeKeyFromInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).removePoint(index);
    } else {
      mutation.influence(key).removePoint(index);
    }
  }

  return true;
}

bool addAnimationKeyToPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).addPoint(time, value);
  }
  return true;
}

bool removeAnimationKeyFromPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  {
    auto mutation = primitive->mutate();
    mutation.animation(key).removePoint(index);
  }
  return true;
}

bool updateAnimationKeyInInterpolator(Document* doc, string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, float time, float value) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).updatePoint(index, time, value);
    } else {
      mutation.influence(key).updatePoint(index, time, value);
    }
  }

  return true;
}

bool setInterpolatorEasing(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, bw::core::Easing easing) {
  if (lerperName != "Animation" && lerperName != "Influence") {
    throw EditorException("Unknown interpolator name: " + lerperName);
  }

  {
    auto mutation = primitive->mutate();
    if (lerperName == "Animation") {
      mutation.animation(key).setEasing(index, easing);
    } else {
      mutation.influence(key).setEasing(index, easing);
    }
  }

  return true;
}

bool addTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  auto newTransform = bw::core::tTransform::makePassthroughPrevious();
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->addScaleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->addAngleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->addOrbitAngleTransform(newTransform);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->addOrbitDistanceTransform(newTransform);
      break;
  }

  return true;
}

bool removeTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index) {
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->removeScaleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->removeAngleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->removeOrbitAngleTransform(index);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->removeOrbitDistanceTransform(index);
      break;
  }

  return true;
}

bool swapTransforms(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index1, uint32_t index2) {
  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      primitive->swapScaleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      primitive->swapAngleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      primitive->swapOrbitAngleTransforms(index1, index2);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      primitive->swapOrbitDistanceTransforms(index1, index2);
      break;
  }

  return true;
}

bool setTransformOperand(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t operandIndex, bw::core::tTransform::OperandType operand) {
  primitive->setTransformOperand(key, transformIndex, operandIndex, operand);
  return true;
}

bool setTransformInput(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t inputIndex, bw::core::InputType input) {
  primitive->setTransformInput(key, transformIndex, inputIndex, input);
  return true;
}

bool setTransformConstant(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t constantIndex, float constant) {
  primitive->setTransformConstant(key, transformIndex, constantIndex, constant);
  return true;
}

bool setTransformFnMultiplier(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t fnMulIndex, float value) {
  primitive->setTransformFnMultiplier(key, transformIndex, fnMulIndex, value);
  return true;
}

bool setTransformTriggerLine(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t indexIndex, uint32_t index) {
  primitive->setTransformTriggerLineIndex(key, transformIndex, indexIndex, index);
  return true;
}

bool setTransformOperation(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, bw::core::tTransform::Operation operation) {
  primitive->setTransformOperation(key, transformIndex, operation);
  return true;
}

}  // namespace editor