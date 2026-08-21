#include <algorithm>

#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/TorusPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/LayerBuildStep.h>

#include "Defines.h"
#include "Actions.h"
#include "EditorException.h"

namespace editor {
using namespace std;

bool recordCurrentState(Document* doc, bool modifying) {
  return modifying;
}

void setEditorMode(Document* doc, Settings& settings, Settings::Mode mode) {
  if (settings.mode == mode) {
    return;
  }

  // A half-drawn Ring means nothing outside the context it was started in.
  doc->disarmMeshDrawTool();

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
  settings.meshSubMode = subMode;
  doc->clearSelections();
}

bool setWorldName(Document* doc, string const& name) {
  auto world = doc->getWorld();

  world->setName(name);
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

namespace {

void rebuildPrefabAuthoringContext(Document* doc, bw::core::Layer* layer) {
  doc->disarmMeshDrawTool();
  doc->clearActiveMesh();
  doc->clearSelections();
  layer->rebuild();

  // The fold filter reads the selected Prefab live (Document.cpp), so which
  // Primitives it admits has just changed - the generator needs telling, the
  // same way switching the active step or editor mode does.
  if (doc->isActive()) {
    doc->getWorld()->getWorldDataGenerator()->refreshPrimitiveFilter();
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

bool setPrefabSize(
    Document*, bw::core::Layer*, bw::core::DefinePrefabs* step, float size) {
  step->setSize(size);
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

bool deleteMeshSubObjects(
    Document* doc, Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  return doc->deleteMeshSubObjects(subMode, indices) > 0;
}

bool splitMeshEdges(Document* doc, set<uint32_t> const& edgeIndices) {
  return doc->splitMeshEdges(edgeIndices) > 0;
}

bool fillMeshHole(Document* doc, uint32_t holeRingIndex) {
  return doc->fillMeshHole(holeRingIndex);
}

bool recentreActiveMesh(Document* doc) {
  return doc->recentreActiveMesh();
}

bool createMeshPrimitiveFromDrawnRing(Document* doc) {
  auto createsNewPrimitive = doc->meshDrawCreatesNewPrimitive();
  auto createsHole = doc->meshDrawCreatesHole();
  auto* mesh = doc->closeMeshDrawRing();
  if (!mesh) {
    return false;
  }

  if (createsNewPrimitive) {
    setPrimitiveDefaultMaterials(mesh);
  }
  if (createsHole) {
    // closeMeshDrawRing has committed the active geometry proxy back to the
    // MeshPrimitive at this point. Request generation here, against that
    // authored topology, rather than relying on subsequent proxy movement to
    // emit a Primitive event. The transaction's normal request may coalesce
    // with this one; both snapshot the completed hole.
    doc->getWorld()->generateClipping(true);
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

bool clonePrimitive(Document* doc, uint32_t primitiveIndex) {
  auto world = doc->getWorld();
  if (!world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  auto primitive = world->getPrimitive(primitiveIndex);

  world->addPrimitive(primitive->copy());
  return true;
}

bool decomposeMeshPrimitive(Document* doc, uint32_t primitiveIndex) {
  auto world = doc->getWorld();
  auto* source = dynamic_cast<bw::core::MeshPrimitive*>(
      world->getPrimitive(primitiveIndex));
  if (!source ||
      !world->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
    return false;
  }

  struct RingPart {
    uint32_t complex;
    uint32_t ring;
    bool filled;
    bw::core::ClosedPolygon const* vertices;
    int parent{-1};
    vector<uint32_t> children;
  };
  vector<RingPart> parts;
  auto const& polygons = source->getVertices();
  for (uint32_t complex = 0; complex < polygons.size(); ++complex) {
    auto outer = uint32_t(parts.size());
    parts.push_back({complex, 0, true, &polygons[complex][0]});
    for (uint32_t ring = 1; ring < polygons[complex].size(); ++ring) {
      auto child = uint32_t(parts.size());
      parts.push_back({complex, ring, false, &polygons[complex][ring],
                       int(outer)});
      parts[outer].children.push_back(child);
    }
  }
  if (parts.size() <= 1) {
    return false;
  }

  auto twiceArea = [](bw::core::ClosedPolygon const& ring) {
    float area = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
      auto const& a = ring[i].p;
      auto const& b = ring[(i + 1) % ring.size()].p;
      area += a.x * b.y - b.x * a.y;
    }
    return area;
  };
  auto contains = [](bw::core::ClosedPolygon const& ring,
                     wp::Vector2 const& point) {
    bool inside = false;
    for (size_t i = 0, previous = ring.size() - 1;
         i < ring.size(); previous = i++) {
      auto const& a = ring[i].p;
      auto const& b = ring[previous].p;
      if ((a.y > point.y) != (b.y > point.y) &&
          point.x < (b.x - a.x) * (point.y - a.y) /
                            (b.y - a.y) +
                        a.x) {
        inside = !inside;
      }
    }
    return inside;
  };
  auto coincide = [](bw::core::ClosedPolygon const& first,
                     bw::core::ClosedPolygon const& second) {
    if (first.size() != second.size() || first.empty()) {
      return false;
    }
    for (size_t start = 0; start < second.size(); ++start) {
      if (first[0].p != second[start].p) continue;
      bool forward = true, reverse = true;
      for (size_t i = 0; i < first.size(); ++i) {
        forward &= first[i].p == second[(start + i) % second.size()].p;
        reverse &= first[i].p ==
                   second[(start + second.size() - i) % second.size()].p;
      }
      if (forward || reverse) return true;
    }
    return false;
  };

  // A filled island follows the smallest hole which contains it. Coincident
  // hole/island boundaries are the welded adjacency created by Fill Hole.
  for (uint32_t filled = 0; filled < parts.size(); ++filled) {
    if (!parts[filled].filled) continue;
    float smallestArea = numeric_limits<float>::max();
    int parent = -1;
    for (uint32_t hole = 0; hole < parts.size(); ++hole) {
      if (parts[hole].filled) continue;
      auto area = abs(twiceArea(*parts[hole].vertices));
      if (area < smallestArea &&
          (coincide(*parts[hole].vertices, *parts[filled].vertices) ||
           contains(*parts[hole].vertices,
                    parts[filled].vertices->front().p))) {
        parent = int(hole);
        smallestArea = area;
      }
    }
    if (parent >= 0) {
      parts[filled].parent = parent;
      parts[parent].children.push_back(filled);
    }
  }

  vector<pair<uint32_t, uint32_t>> ordered;  // part, containment depth
  auto visit = [&](auto&& self, uint32_t part, uint32_t depth) -> void {
    ordered.push_back({part, depth});
    for (auto child : parts[part].children) self(self, child, depth + 1);
  };
  for (uint32_t part = 0; part < parts.size(); ++part) {
    if (parts[part].parent < 0) visit(visit, part, 0);
  }
  if (ordered.size() != parts.size()) {
    return false;
  }

  vector<bw::core::Primitive*> created;
  created.reserve(ordered.size());
  for (auto [partIndex, depth] : ordered) {
    auto* part = static_cast<bw::core::MeshPrimitive*>(source->copy());
    world->addPrimitive(part);
    if (!part->retainRing(parts[partIndex].complex, parts[partIndex].ring)) {
      throw EditorException("Could not retain a MeshPrimitive Ring while decomposing.");
    }
    part->setOperation(parts[partIndex].filled
                           ? bw::core::Primitive::Operation::Union
                           : bw::core::Primitive::Operation::Difference);
    part->setPriority(uint8_t(min<uint32_t>(
        BW_PRIORITY_MAX_VALUE, uint32_t(source->getPriority()) + depth)));
    created.push_back(part);
  }

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

bool setPrimitivePriority(Document* doc, bw::core::Primitive* primitive, uint8_t priority) {
  primitive->setPriority(priority);
  return true;
}

bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive) {
  int priority = (int)primitive->getPriority();
  int newPriority = min(255, priority + 1);

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