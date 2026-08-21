#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <format>
#include <limits>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <yaml-cpp/yaml.h>

#include <willpower/common/MathsUtils.h>
#include <willpower/geometry/Edge.h>
#include <willpower/geometry/MeshOperations.h>
#include <willpower/geometry/MeshValidator.h>
#include <willpower/geometry/Polygon.h>
#include <willpower/geometry/Vertex.h>

#include "core/BinarySerializer.h"
#include "core/DefinePrefabs.h"
#include "core/YamlSerializer.h"
#include "core/RegularPolygon.h"
#include "core/DynamicWorldDataGenerator.h"
#include "core/Vertex.h"
#include "core/MeshPrimitive.h"
#include "core/LayerBuildStep.h"

#include "common/GameDefines.h"

#include "Defines.h"
#include "Document.h"
#include "EditorException.h"
#include "AppHelpers.h"
#include "Undo.h"

extern spdlog::logger* gLogger;

namespace editor {
using namespace std;

Document* Document::msInstance = nullptr;

bool primitiveVisibleForActiveStep(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings) {
  // The ghost previews what "Create Primitive" would add next, so it only
  // makes sense while the active step can accept a new Primitive (ADR-0015):
  // a DefinePrefabs step with no Prefab selected, and a PrefabField step
  // (which never accepts one), both hide it the same way Mesh mode does.
  // Mesh mode neither draws it nor lets anything touch it, and the fold must
  // not see it there either: a Primitive hidden from the overlay while still
  // contributing geometry would read as a phantom shape.
  if (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return settings.mode != Settings::Mode::Mesh &&
           layer.getActiveStep()->acceptsNewPrimitives();
  }

  auto owningStepIndex = layer.getOwningStepIndex(primitive);
  if (owningStepIndex != ~0u &&
      dynamic_cast<bw::core::DefinePrefabs const*>(
          layer.getStep(owningStepIndex))) {
    return owningStepIndex == layer.getActiveStepIndex();
  }

  return settings.showAllStepPrimitives || owningStepIndex == ~0u ||
         owningStepIndex <= layer.getActiveStepIndex();
}

bool primitiveParticipatesInEditorFold(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings) {
  // Editing a Prefab clips that Prefab in isolation - its Primitives have no
  // world-space relationship to the rest of the Layer (ADR-0017), so while
  // it is the active step they are the only thing the fold admits for this
  // Layer at all, not an addition to the Layer's ordinary content.
  auto const* activeDefinePrefabs =
      dynamic_cast<bw::core::DefinePrefabs const*>(layer.getActiveStep());
  if (activeDefinePrefabs && activeDefinePrefabs->getSelectedPrefab()) {
    return activeDefinePrefabs->ownsPrimitive(primitive);
  }

  auto owningStepIndex = layer.getOwningStepIndex(primitive);
  if (owningStepIndex != ~0u &&
      dynamic_cast<bw::core::DefinePrefabs const*>(
          layer.getStep(owningStepIndex))) {
    return false;
  }
  return primitiveVisibleForActiveStep(layer, primitive, settings);
}

namespace {

// Selection's "current context" (the active Layer, and - unless
// showAllStepPrimitives opts out of the boundary - primitives no later than
// its active step) mirrors the world view's own visibility rule
// (editor::primitiveVisibleForActiveStep): a Primitive nothing draws should
// be nothing a click, a box, or Select All can pick up either. World's
// index-space is already scoped to the active Layer (World::getPrimitive et
// al forward to getActiveLayer()), so only the step boundary needs adding
// here.
bool filledHoleHasWeldedIsland(
    wp::geometry::Mesh const& mesh, uint32_t polygonIndex) {
  auto const& polygon = mesh.getPolygon(polygonIndex);
  if (!polygon.isHole()) {
    return false;
  }
  auto const edges = polygon.getEdgeIndexSet();
  for (auto candidateIndex = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(candidateIndex);
       candidateIndex = mesh.getNextPolygonIndex(candidateIndex)) {
    auto const& candidate = mesh.getPolygon(candidateIndex);
    if (!candidate.isHole() && candidate.getEdgeIndexSet() == edges) {
      return true;
    }
  }
  return false;
}

bool pointInsideRing(
    wp::geometry::Mesh const& mesh,
    wp::geometry::Polygon const& ring,
    wp::Vector2 const& point) {
  auto vertices = ring.getOrderedVertexIndices();
  if (vertices.size() < 3) {
    return false;
  }
  bool inside = false;
  for (size_t i = 0, previous = vertices.size() - 1; i < vertices.size(); previous = i++) {
    auto const& a = mesh.getVertex(vertices[i]).getPosition();
    auto const& b = mesh.getVertex(vertices[previous]).getPosition();
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

set<uint32_t> getIgnoredPrimitiveIndices(bw::core::World const& world, Settings const& settings) {
  set<uint32_t> ignores;

  if (!settings.ghostActive) {
    ignores.insert(0);
  }

  auto* activeLayer = world.getActiveLayer();

  for (uint32_t i = 0; i < world.getNumPrimitives(); ++i) {
    auto* primitive = world.getPrimitive(i);

    if (!settings.renderAnimatedPrimitives && !primitive->isStatic()) {
      ignores.insert(i);
      continue;
    }

    // Mesh mode only exposes authored MeshPrimitives to the viewport. The
    // rest of the editor's objects remain visible but cannot receive a
    // hover, click, or box selection there.
    if (settings.mode == Settings::Mode::Mesh &&
        !dynamic_cast<bw::core::MeshPrimitive const*>(primitive)) {
      ignores.insert(i);
      continue;
    }

    if (activeLayer && !primitiveVisibleForActiveStep(*activeLayer, primitive, settings)) {
      ignores.insert(i);
      continue;
    }

    if (activeLayer) {
      auto owningStepIndex = activeLayer->getOwningStepIndex(primitive);
      if (settings.mode == Settings::Mode::Mesh &&
          owningStepIndex != activeLayer->getActiveStepIndex()) {
        ignores.insert(i);
      } else if (owningStepIndex != ~0u &&
                 !activeLayer->getStep(owningStepIndex)->permitsDirectPrimitiveEditing()) {
        ignores.insert(i);
      }
    }
  }

  return ignores;
}

}  // namespace

Document::Document()
    : mModified(false), mSelectedWorldVertexIndex(~0u), mSelectedTriggerLineIndex(~0u), mPlayerOldProxyPosition({0, 0}), mPlayerProxyPosition({0, 0}), mPlayerProxyAngle(0.0f), mPlayerOldProxyAngle(0.0f) {
}

Document::~Document() {
}

Document* Document::instance() {
  if (!msInstance) {
    msInstance = new Document();
  }

  return msInstance;
}

void Document::reset() {
  clearUndoHistory();
  mModified = false;
  mFilepath = "";
  mWorld.reset();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
  mSelectedPrimitiveIndices.clear();
  clearActiveMesh();
  disarmMeshDrawTool();
  mMeshHoverExplanation.clear();
  mPlayerProxyPosition.set(0.0f, 0.0f);
  mPlayerProxyAngle = 0.0f;
}

bool Document::isActive() const {
  return mWorld != nullptr;
}

void Document::setModified(bool modified) {
  mModified = modified;
}

bool Document::isModified() const {
  return mModified;
}

string const& Document::getFilepath() const {
  return mFilepath;
}

bool Document::hasFilepath() const {
  return mFilepath != "";
}

void Document::setWorld(bw::core::World const& world) {
  mWorld = make_shared<bw::core::World>(world);
}

WorldSnapshot Document::captureWorldSnapshot() const {
  if (!mWorld) {
    throw EditorException("Cannot snapshot an inactive document.");
  }

  auto serializer = shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  auto workData = bw::core::SerializationWorkData{};
  workData.markSerializedUnmodified = false;
  workData.includeGhostPrimitives = true;
  mWorld->serialize(serializer, workData);
  serializer->serialize();

  WorldSnapshot snapshot;
  snapshot.serializedWorld = serializer->getSerializedString();
  snapshot.accelerationGridSize = mWorld->getPrimitiveAccelerationGridSize();
  snapshot.alwaysUpdateWorldVertices = mWorld->getAlwaysUpdateVertices();

  auto generator = mWorld->getWorldDataGenerator();
  snapshot.layerSelection = generator->getLayerSelection();
  if (auto dynamicGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator const*>(generator)) {
    snapshot.hasDynamicGenerator = true;
    snapshot.alwaysUpdateGeneratorVertices = dynamicGenerator->getAlwaysUpdateVertices();
    snapshot.allowCommitIfVisible = dynamicGenerator->getAllowCommitIfVisible();
    snapshot.scheduledGenerationInterval = dynamicGenerator->getScheduledGenerationInterval();
  }

  return snapshot;
}

void Document::restoreWorldSnapshot(WorldSnapshot const& snapshot) {
  auto serializer = shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(snapshot.serializedWorld));
  serializer->deserialize();

  auto world = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  world->removePrimitive(uint32_t(ED_GHOST_INDEX));

  auto workData = bw::core::SerializationWorkData{};
  workData.accelGridSize = snapshot.accelerationGridSize;
  workData.allowEmptyWorld = true;
  if (!world->deserialize(serializer, workData)) {
    throw EditorException("Could not restore the world snapshot.");
  }

  world->setAlwaysUpdateVertices(snapshot.alwaysUpdateWorldVertices);
  auto generator = world->getWorldDataGenerator();
  generator->setLayerSelection(snapshot.layerSelection);
  if (snapshot.hasDynamicGenerator) {
    auto dynamicGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(generator);
    dynamicGenerator->setAlwaysUpdateVertices(snapshot.alwaysUpdateGeneratorVertices);
    dynamicGenerator->setAllowCommitIfVisible(snapshot.allowCommitIfVisible);
    dynamicGenerator->setScheduledGenerationInterval(snapshot.scheduledGenerationInterval);
  }

  mWorld = move(world);
}

void Document::setPrimitiveFilter(bw::core::PrimitiveFilter filter) {
  mPrimitiveFilter = move(filter);

  if (mWorld) {
    mWorld->getWorldDataGenerator()->setPrimitiveFilter(mPrimitiveFilter);
  }
}

shared_ptr<bw::core::World> Document::getWorld() {
  return mWorld;
}

void Document::setSelectedWorldVertexIndex(uint32_t index) {
  clearSelections();
  mSelectedWorldVertexIndex = index;
}

void Document::setSelectedTriggerLineIndex(uint32_t index) {
  clearSelections();
  mSelectedTriggerLineIndex = index;
}

void Document::setSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  clearSelections();
  mSelectedPrimitiveIndices = indices;
}

void Document::addSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.insert(index);
}

void Document::addSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  mSelectedPrimitiveIndices.insert(indices.begin(), indices.end());
}

void Document::removeSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.erase(index);
}

void Document::removeSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  for (auto index : indices) {
    mSelectedPrimitiveIndices.erase(index);
  }
}

void Document::clearSelections() {
  mSelectedPrimitiveIndices.clear();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
  clearMeshSelections();
}

void Document::clearMeshSelections() {
  mSelectedMeshVertexIndices.clear();
  mSelectedMeshEdgeIndices.clear();
  mSelectedMeshRingIndices.clear();
}

void Document::revalidateSelection() {
  if (!mWorld) {
    return;
  }

  auto numPrimitives = mWorld->getNumPrimitives();

  for (auto it = mSelectedPrimitiveIndices.begin(); it != mSelectedPrimitiveIndices.end();) {
    if (*it >= numPrimitives) {
      it = mSelectedPrimitiveIndices.erase(it);
    } else {
      ++it;
    }
  }

  if (mSelectedTriggerLineIndex != ~0u && mSelectedTriggerLineIndex >= mWorld->getNumTriggerLines()) {
    mSelectedTriggerLineIndex = ~0u;
  }

  // mSelectedWorldVertexIndex names a vertex in the asynchronously
  // regenerated WorldData, which Document has no synchronous handle on here,
  // so there is no live count to bound-check it against. It is left alone:
  // nothing dereferences it as an array index (only compares it to ~0u), so
  // unlike the two selections above it cannot cause an out-of-bounds read.
}

DocumentHover Document::getHover(
    wp::Vector2 const& mouseWorldPos,
    Settings const& settings,
    bw::core::WorldData const* worldData) const {
  if (!isActive()) {
    return {};
  }

  if (settings.mode == Settings::Mode::Mesh) {
    auto subObjectIndices = getHoveredMeshSubObjectIndices(mouseWorldPos, settings);
    if (!subObjectIndices.empty()) {
      return {HoverableType::MeshSubObject, std::move(subObjectIndices)};
    }
    auto primitiveIndices = getHoveredPrimitiveIndices(mouseWorldPos, settings);
    return primitiveIndices.empty()
               ? DocumentHover{}
               : DocumentHover{HoverableType::Primitive, std::move(primitiveIndices)};
  }

  if (worldData) {
    auto worldVertexIndex = static_cast<uint32_t>(
        worldData->getNearestVertexIndex(mouseWorldPos, 3.0f));
    if (worldVertexIndex != ~0u) {
      return {HoverableType::WorldVertex, {worldVertexIndex}};
    }
  }

  auto triggerLineIndex = getHoveredTriggerLineIndex(mouseWorldPos, settings);
  if (triggerLineIndex != ~0u) {
    return {HoverableType::TriggerLine, {triggerLineIndex}};
  }

  auto primitiveIndices = getHoveredPrimitiveIndices(mouseWorldPos, settings);
  if (!primitiveIndices.empty()) {
    return {HoverableType::Primitive, std::move(primitiveIndices)};
  }

  return {};
}

uint32_t Document::getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  if (!isActive()) {
    return ~0u;
  }

  // Taken from the ordered list rather than from World::findPrimitiveIndex,
  // so the single hovered Primitive is the same one a click would select -
  // the ghost, where it overlaps something.
  auto hovered = getHoveredPrimitiveIndices(mouseWorldPos, settings);

  return hovered.empty() ? ~0u : hovered.front();
}

vector<uint32_t> Document::getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  auto hovered = mWorld->findPrimitiveIndices(
      mouseWorldPos, true, getIgnoredPrimitiveIndices(*mWorld, settings));

  // Ghost first, so the first click of a click-through cycle lands on it; the
  // rest keep their order, so clicking again still walks what is underneath.
  auto ghost = find(hovered.begin(), hovered.end(), uint32_t(ED_GHOST_INDEX));

  if (ghost != hovered.end()) {
    rotate(hovered.begin(), ghost, ghost + 1);
  }

  return hovered;
}

string Document::meshIneligibilityReason(uint32_t primitiveIndex) const {
  if (!mWorld || primitiveIndex >= mWorld->getNumPrimitives()) {
    return "Nothing under the cursor.";
  }

  auto* primitive = mWorld->getPrimitive(primitiveIndex);
  if (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return "The ghost is not an authored MeshPrimitive.";
  }
  if (!dynamic_cast<bw::core::MeshPrimitive*>(primitive)) {
    return "The Primitive under the cursor is not a MeshPrimitive.";
  }

  auto* layer = mWorld->getActiveLayer();
  auto owningStep = layer->getOwningStepIndex(primitive);
  if (owningStep != layer->getActiveStepIndex()) {
    return "This MeshPrimitive belongs to another LayerBuildStep.";
  }
  if (!layer->getStep(owningStep)->permitsDirectPrimitiveEditing()) {
    return "The selected LayerBuildStep does not permit direct editing.";
  }
  return {};
}

uint32_t Document::getPrimitiveIndexAt(wp::Vector2 const& worldPosition) const {
  if (!mWorld) {
    return ~0u;
  }
  auto hits = mWorld->findPrimitiveIndices(worldPosition, true, {});
  hits.erase(remove(hits.begin(), hits.end(), uint32_t(ED_GHOST_INDEX)), hits.end());
  return hits.empty() ? ~0u : hits.front();
}

bool Document::activateMesh(uint32_t primitiveIndex) {
  if (!meshIneligibilityReason(primitiveIndex).empty()) {
    return false;
  }
  if (mActiveMesh && mActiveMeshPrimitiveIndex == primitiveIndex) {
    return true;
  }
  auto* primitive = static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(primitiveIndex));
  mActiveMesh = primitive->createGeometryProxy();
  mActiveMeshPrimitiveIndex = primitiveIndex;
  clearMeshSelections();
  return true;
}

void Document::clearActiveMesh() {
  mActiveMesh.reset();
  mActiveMeshPrimitiveIndex = ~0u;
  clearMeshSelections();
}

uint32_t Document::getActiveMeshPrimitiveIndex() const {
  return mActiveMeshPrimitiveIndex;
}

wp::geometry::Mesh const* Document::getActiveMesh() const {
  return mActiveMesh.get();
}

vector<uint32_t> Document::getHoveredMeshSubObjectIndices(
    wp::Vector2 const& worldPosition, Settings const& settings) const {
  vector<uint32_t> result;
  if (!mActiveMesh || settings.mode != Settings::Mode::Mesh) {
    return result;
  }

  if (settings.meshSubMode == Settings::MeshSubMode::Vertex) {
    auto radiusSq = settings.meshVertexPickRadius * settings.meshVertexPickRadius;
    for (auto index = mActiveMesh->getFirstVertexIndex();
         !mActiveMesh->vertexIndexIterationFinished(index);
         index = mActiveMesh->getNextVertexIndex(index)) {
      if (mActiveMesh->getVertex(index).getPosition().distanceToSq(worldPosition) <= radiusSq) {
        result.push_back(index);
      }
    }
  } else if (settings.meshSubMode == Settings::MeshSubMode::Edge) {
    for (auto index = mActiveMesh->getFirstEdgeIndex();
         !mActiveMesh->edgeIndexIterationFinished(index);
         index = mActiveMesh->getNextEdgeIndex(index)) {
      if (mActiveMesh->getEdge(index).getDistanceTo(worldPosition) <=
          settings.meshEdgeSelectionDistance) {
        result.push_back(index);
      }
    }
  } else {
    // A boundary hit is explicit: select the Ring which owns that edge before
    // considering the potentially stacked Rings containing the cursor. This
    // makes a hole directly selectable without relying on click cycling.
    set<uint32_t> edgeOwners;
    for (auto edgeIndex = mActiveMesh->getFirstEdgeIndex();
         !mActiveMesh->edgeIndexIterationFinished(edgeIndex);
         edgeIndex = mActiveMesh->getNextEdgeIndex(edgeIndex)) {
      auto const& edge = mActiveMesh->getEdge(edgeIndex);
      if (edge.getDistanceTo(worldPosition) <=
          settings.meshEdgeSelectionDistance) {
        edgeOwners.insert(
            edge.getPolygonReferences().begin(),
            edge.getPolygonReferences().end());
      }
    }
    if (!edgeOwners.empty()) {
      for (auto index : edgeOwners) {
        if (!filledHoleHasWeldedIsland(*mActiveMesh, index)) {
          result.push_back(index);
        }
      }
      return result;
    }

    for (auto index = mActiveMesh->getFirstPolygonIndex();
         !mActiveMesh->polygonIndexIterationFinished(index);
         index = mActiveMesh->getNextPolygonIndex(index)) {
      if (!filledHoleHasWeldedIsland(*mActiveMesh, index) &&
          pointInsideRing(*mActiveMesh, mActiveMesh->getPolygon(index), worldPosition)) {
        result.push_back(index);
      }
    }
  }
  return result;
}

set<uint32_t> Document::getMeshSubObjectIndicesInBounds(
    wp::BoundingBox const& worldBounds, Settings const& settings) const {
  set<uint32_t> result;
  if (!mActiveMesh || settings.mode != Settings::Mode::Mesh) {
    return result;
  }

  if (settings.meshSubMode == Settings::MeshSubMode::Vertex) {
    return mActiveMesh->getVertexIndicesInBoundingBox(worldBounds);
  }
  if (settings.meshSubMode == Settings::MeshSubMode::Edge) {
    for (auto index = mActiveMesh->getFirstEdgeIndex();
         !mActiveMesh->edgeIndexIterationFinished(index);
         index = mActiveMesh->getNextEdgeIndex(index)) {
      auto const& edge = mActiveMesh->getEdge(index);
      if (worldBounds.pointInside(mActiveMesh->getVertex(edge.getFirstVertex()).getPosition()) &&
          worldBounds.pointInside(mActiveMesh->getVertex(edge.getSecondVertex()).getPosition())) {
        result.insert(index);
      }
    }
  } else {
    for (auto index = mActiveMesh->getFirstPolygonIndex();
         !mActiveMesh->polygonIndexIterationFinished(index);
         index = mActiveMesh->getNextPolygonIndex(index)) {
      if (filledHoleHasWeldedIsland(*mActiveMesh, index)) {
        continue;
      }
      auto const vertices = mActiveMesh->getPolygon(index).getVertexIndexSet();
      if (all_of(vertices.begin(), vertices.end(), [&](uint32_t vertexIndex) {
            return worldBounds.pointInside(mActiveMesh->getVertex(vertexIndex).getPosition());
          })) {
        result.insert(index);
      }
    }
  }
  return result;
}

set<uint32_t> Document::getSelectableMeshSubObjectIndices(
    Settings::MeshSubMode subMode) const {
  set<uint32_t> result;
  if (!mActiveMesh) {
    return result;
  }
  if (subMode == Settings::MeshSubMode::Vertex) {
    for (auto index = mActiveMesh->getFirstVertexIndex();
         !mActiveMesh->vertexIndexIterationFinished(index);
         index = mActiveMesh->getNextVertexIndex(index)) result.insert(index);
  } else if (subMode == Settings::MeshSubMode::Edge) {
    for (auto index = mActiveMesh->getFirstEdgeIndex();
         !mActiveMesh->edgeIndexIterationFinished(index);
         index = mActiveMesh->getNextEdgeIndex(index)) result.insert(index);
  } else {
    for (auto index = mActiveMesh->getFirstPolygonIndex();
         !mActiveMesh->polygonIndexIterationFinished(index);
         index = mActiveMesh->getNextPolygonIndex(index)) {
      if (!filledHoleHasWeldedIsland(*mActiveMesh, index)) {
        result.insert(index);
      }
    }
  }
  return result;
}

set<uint32_t> const& Document::getSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode) const {
  if (subMode == Settings::MeshSubMode::Vertex) return mSelectedMeshVertexIndices;
  if (subMode == Settings::MeshSubMode::Edge) return mSelectedMeshEdgeIndices;
  return mSelectedMeshRingIndices;
}

set<uint32_t> const& Document::getSelectedMeshVertexIndices() const { return mSelectedMeshVertexIndices; }
set<uint32_t> const& Document::getSelectedMeshEdgeIndices() const { return mSelectedMeshEdgeIndices; }
set<uint32_t> const& Document::getSelectedMeshRingIndices() const { return mSelectedMeshRingIndices; }

void Document::setSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  clearSelections();
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  *selection = indices;
}

void Document::addSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  selection->insert(indices.begin(), indices.end());
}

void Document::toggleSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  for (auto index : indices) {
    if (!selection->erase(index)) selection->insert(index);
  }
}

void Document::restoreMeshSelection(
    uint32_t activeMeshPrimitiveIndex, set<uint32_t> const& vertices,
    set<uint32_t> const& edges, set<uint32_t> const& rings) {
  clearActiveMesh();
  if (activeMeshPrimitiveIndex != ~0u && activateMesh(activeMeshPrimitiveIndex)) {
    mSelectedMeshVertexIndices = vertices;
    mSelectedMeshEdgeIndices = edges;
    mSelectedMeshRingIndices = rings;
  }
}

void Document::setMeshHoverExplanation(string explanation) {
  mMeshHoverExplanation = move(explanation);
}

string const& Document::getMeshHoverExplanation() const {
  return mMeshHoverExplanation;
}

namespace {

set<uint32_t> affectedMeshVertices(
    wp::geometry::Mesh const& mesh, Settings::MeshSubMode subMode,
    set<uint32_t> const& selection) {
  set<uint32_t> vertices;
  if (subMode == Settings::MeshSubMode::Vertex) {
    vertices = selection;
  } else if (subMode == Settings::MeshSubMode::Edge) {
    for (auto edgeIndex : selection) {
      auto const& edge = mesh.getEdge(edgeIndex);
      vertices.insert(edge.getFirstVertex());
      vertices.insert(edge.getSecondVertex());
    }
  } else {
    auto includeRing = [&](uint32_t polygonIndex) {
      auto polygonVertices = mesh.getPolygon(polygonIndex).getVertexIndexSet();
      vertices.insert(polygonVertices.begin(), polygonVertices.end());
    };
    for (auto polygonIndex : selection) {
      auto const& polygon = mesh.getPolygon(polygonIndex);
      includeRing(polygonIndex);

      // Storage explicitly links an outer Ring to its holes, while filled
      // islands are deliberately top-level ComplexPolygons whose hierarchy is
      // re-derived geometrically. Include every Ring contained by the selected
      // Ring so the complete alternating hole/island hierarchy translates as
      // one rigid group.
      for (auto candidateIndex = mesh.getFirstPolygonIndex();
           !mesh.polygonIndexIterationFinished(candidateIndex);
           candidateIndex = mesh.getNextPolygonIndex(candidateIndex)) {
        if (candidateIndex == polygonIndex) {
          continue;
        }
        auto const& candidate = mesh.getPolygon(candidateIndex);
        auto const orderedVertices = candidate.getOrderedVertexIndices();
        if (!orderedVertices.empty() &&
            pointInsideRing(
                mesh, polygon,
                mesh.getVertex(orderedVertices.front()).getPosition())) {
          includeRing(candidateIndex);
        }
      }
    }
  }
  return vertices;
}

// MeshValidator::validateVertexMove is meant to be asked "what if this
// vertex moved by delta", against a mesh where it has NOT yet moved -
// querying it with a zero move after actually applying the move makes the
// query point coincide exactly with a real mesh vertex, which trips
// point-in-polygon's own boundary handling and reports false containment
// violations against a hole's outer. So fullyMoved (every affected vertex
// already at its final position, giving co-selected neighbours their
// correct final positions for the crossing/containment checks) has each
// vertex tested by momentarily setting it back to its start position and
// calling validateVertexMove with the real delta, exactly as the API
// expects, then restoring it before testing the next one.
bool meshGroupMoveIsValid(
    wp::geometry::Mesh& fullyMoved,
    wp::geometry::Mesh const& startPositions,
    set<uint32_t> const& affectedVertices,
    wp::Vector2 const& delta) {
  // Whole Rings translated together preserve their internal shape and
  // containment. Validate such a group against stationary Rings as rigid
  // geometry instead of feeding its vertices one at a time to MeshValidator:
  // those transient partial-Ring states can incorrectly report that a hole
  // escaped an outer which is moving by the same delta.
  bool completeRings = true;
  for (auto polygonIndex = startPositions.getFirstPolygonIndex();
       !startPositions.polygonIndexIterationFinished(polygonIndex);
       polygonIndex = startPositions.getNextPolygonIndex(polygonIndex)) {
    auto const ringVertices =
        startPositions.getPolygon(polygonIndex).getVertexIndexSet();
    auto affectedCount = count_if(
        ringVertices.begin(), ringVertices.end(), [&](auto vertex) {
          return affectedVertices.contains(vertex);
        });
    if (affectedCount != 0 && affectedCount != ringVertices.size()) {
      completeRings = false;
      break;
    }
  }

  wp::geometry::IndexVector movedEdges;
  wp::geometry::IndexVector stationaryEdges;
  if (completeRings) {
    for (auto edgeIndex = startPositions.getFirstEdgeIndex();
         !startPositions.edgeIndexIterationFinished(edgeIndex);
         edgeIndex = startPositions.getNextEdgeIndex(edgeIndex)) {
      auto const& edge = startPositions.getEdge(edgeIndex);
      auto firstMoved = affectedVertices.contains(edge.getFirstVertex());
      auto secondMoved = affectedVertices.contains(edge.getSecondVertex());
      if (firstMoved != secondMoved) {
        completeRings = false;
        break;
      }
      (firstMoved ? movedEdges : stationaryEdges).push_back(edgeIndex);
    }
  }

  if (completeRings) {
    auto crosses = [](wp::Vector2 const& a0, wp::Vector2 const& a1,
                      wp::Vector2 const& b0, wp::Vector2 const& b1) {
      auto type = wp::MathsUtils::lineIntersectsLine(a0, a1, b0, b1);
      return type != wp::MathsUtils::NotIntersecting &&
             type != wp::MathsUtils::Touching;
    };
    // A translating Ring crosses a stationary Ring iff either a moved vertex
    // sweeps through a stationary edge, or (in the moved Ring's reference
    // frame) a stationary vertex sweeps through a moved edge.
    for (auto vertex : affectedVertices) {
      auto from = startPositions.getVertex(vertex).getPosition();
      for (auto edgeIndex : stationaryEdges) {
        auto const& edge = startPositions.getEdge(edgeIndex);
        if (crosses(
                from, from + delta,
                startPositions.getVertex(edge.getFirstVertex()).getPosition(),
                startPositions.getVertex(edge.getSecondVertex()).getPosition())) {
          return false;
        }
      }
    }
    for (auto vertex = startPositions.getFirstVertexIndex();
         !startPositions.vertexIndexIterationFinished(vertex);
         vertex = startPositions.getNextVertexIndex(vertex)) {
      if (affectedVertices.contains(vertex)) {
        continue;
      }
      auto from = startPositions.getVertex(vertex).getPosition();
      for (auto edgeIndex : movedEdges) {
        auto const& edge = startPositions.getEdge(edgeIndex);
        if (crosses(
                from, from - delta,
                startPositions.getVertex(edge.getFirstVertex()).getPosition(),
                startPositions.getVertex(edge.getSecondVertex()).getPosition())) {
          return false;
        }
      }
    }
    return true;
  }

  wp::geometry::MeshValidator validator(&fullyMoved);
  for (auto vertexIndex : affectedVertices) {
    auto startPosition = startPositions.getVertex(vertexIndex).getPosition();
    auto finalPosition = startPosition + delta;
    fullyMoved.moveVertexTo(vertexIndex, startPosition);
    auto result = validator.validateVertexMove(vertexIndex, delta);
    fullyMoved.moveVertexTo(vertexIndex, finalPosition);

    bool holeVertex = false;
    for (auto polygonIndex = fullyMoved.getFirstPolygonIndex();
         !fullyMoved.polygonIndexIterationFinished(polygonIndex);
         polygonIndex = fullyMoved.getNextPolygonIndex(polygonIndex)) {
      auto const& polygon = fullyMoved.getPolygon(polygonIndex);
      if (polygon.isHole() && polygon.getVertexIndexSet().contains(vertexIndex)) {
        holeVertex = true;
        break;
      }
    }
    // MeshValidator's containment query currently treats the hole's owning
    // outer Polygon as an unrelated Polygon (see its TODO about polygons that
    // contain polygonRef polygons as holes). That makes a valid outward move
    // report VertexInPolygon merely because the vertex remains inside its
    // outer. Edge crossing remains authoritative for attempts to escape.
    auto onlyOwningOuterReported =
        holeVertex && result == wp::geometry::MeshValidator::VertexInPolygon;
    if (result != wp::geometry::MeshValidator::Valid &&
        !onlyOwningOuterReported) {
      return false;
    }
  }
  return true;
}

// Ordered world-space positions of a Ring's vertices, for the geometric
// checks below.
vector<wp::Vector2> ringPositions(wp::geometry::Mesh const& mesh, uint32_t polygonIndex) {
  vector<wp::Vector2> points;
  for (auto vertexIndex : mesh.getPolygon(polygonIndex).getOrderedVertexIndices()) {
    points.push_back(mesh.getVertex(vertexIndex).getPosition());
  }
  return points;
}

bool segmentsCross(wp::Vector2 const& a0, wp::Vector2 const& a1, wp::Vector2 const& b0, wp::Vector2 const& b1) {
  auto type = wp::MathsUtils::lineIntersectsLine(a0, a1, b0, b1);
  return type != wp::MathsUtils::NotIntersecting && type != wp::MathsUtils::Touching;
}

// A simple (non-self-intersecting) polygon check over the Ring's own edges,
// skipping edges that share a vertex (including the wraparound pair).
bool ringIsSimple(vector<wp::Vector2> const& points) {
  auto n = points.size();
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 2; j < n; ++j) {
      if (i == 0 && j == n - 1) {
        continue;
      }
      if (segmentsCross(points[i], points[(i + 1) % n], points[j], points[(j + 1) % n])) {
        return false;
      }
    }
  }
  return true;
}

// Whether holePoints, taken as a Ring in their own right, sits entirely
// inside outerPoints without crossing it - checked directly against
// explicit position lists (rather than a live Polygon) so a delete can be
// validated against its *would-be* shape before ever mutating the mesh.
bool pointsFormAValidHoleInOuter(
    vector<wp::Vector2> const& outerPoints, vector<wp::Vector2> const& holePoints) {
  for (auto const& p : holePoints) {
    if (!wp::MathsUtils::pointInPolygon(p, outerPoints)) {
      return false;
    }
  }

  auto numOuter = outerPoints.size(), numHole = holePoints.size();
  for (size_t i = 0; i < numHole; ++i) {
    for (size_t j = 0; j < numOuter; ++j) {
      if (segmentsCross(
              holePoints[i], holePoints[(i + 1) % numHole],
              outerPoints[j], outerPoints[(j + 1) % numOuter])) {
        return false;
      }
    }
  }
  return true;
}

// A vertex/edge deletion's effect is scoped to the one Ring it belongs to:
// the Ring itself must stay simple, and if it is a hole or has holes, the
// containment between it and its counterpart must still hold. Takes the
// Ring's own would-be positions explicitly, since this runs before the
// mesh is actually mutated - every other Ring's positions are read live.
bool ringMutationIsValid(
    wp::geometry::Mesh const& mesh, uint32_t ringIndex,
    vector<wp::Vector2> const& newPositions) {
  if (!ringIsSimple(newPositions)) {
    return false;
  }

  auto const& polygon = mesh.getPolygon(ringIndex);
  if (polygon.isHole()) {
    for (auto outerIndex = mesh.getFirstPolygonIndex();
         !mesh.polygonIndexIterationFinished(outerIndex);
         outerIndex = mesh.getNextPolygonIndex(outerIndex)) {
      auto const& outerPolygon = mesh.getPolygon(outerIndex);
      auto const& holes = outerPolygon.getHoleIndices();
      if (!outerPolygon.isHole() && find(holes.begin(), holes.end(), ringIndex) != holes.end()) {
        return pointsFormAValidHoleInOuter(ringPositions(mesh, outerIndex), newPositions);
      }
    }
    return true;
  }

  for (auto holeIndex : polygon.getHoleIndices()) {
    if (!pointsFormAValidHoleInOuter(newPositions, ringPositions(mesh, holeIndex))) {
      return false;
    }
  }
  return true;
}

// The single Ring a vertex/edge belongs to, or ~0u if it is shared by more
// than one - an adjacency this simple editing model refuses to delete
// through.
uint32_t owningRing(wp::geometry::IndexSet const& polygonRefs) {
  return polygonRefs.size() == 1 ? *polygonRefs.begin() : ~0u;
}

uint32_t addRingSharingBoundary(
    wp::geometry::Mesh& mesh, uint32_t sourcePolygonIndex) {
  wp::geometry::IndexVector edgeData;
  for (auto const& edge : mesh.getPolygon(sourcePolygonIndex).getEdges()) {
    edgeData.insert(edgeData.end(), {edge.v0, edge.v1, edge.index});
  }
  return mesh.addPolygon(wp::geometry::Polygon(edgeData));
}

// Deleting a vertex/edge tombstones mesh sub-objects rather than compacting
// them away immediately, and Mesh's copy/assignment rebinds every edge -
// dead ones included - which touches whichever vertices they still name.
// Copying or assigning a mesh that already has any tombstoned edges is
// therefore unsafe until it has been compact()ed. Every delete below
// mutates its Mesh& argument directly and validates from plain position
// lists computed beforehand, so no such copy ever has to happen mid-batch;
// Document::deleteMeshSubObjects compacts once, after the whole batch, right
// before handing the result back to Document state.
bool deleteOneMeshVertex(wp::geometry::Mesh& mesh, uint32_t vertexIndex) {
  wp::geometry::IndexSet polygonRefs;
  for (auto edgeIndex : mesh.getVertex(vertexIndex).getEdgeReferences()) {
    auto const& refs = mesh.getEdge(edgeIndex).getPolygonReferences();
    polygonRefs.insert(refs.begin(), refs.end());
  }

  if (polygonRefs.empty()) {
    return false;
  }

  // A filled island and the hole beneath it are two Polygon faces over one
  // welded boundary. They may be edited together only when every Ring
  // referencing this vertex uses that exact same vertex loop. Unrelated
  // junctions remain deliberately unsupported.
  auto const& referenceVertices =
      mesh.getPolygon(*polygonRefs.begin()).getVertexIndexSet();
  if (referenceVertices.size() <= 3 ||
      any_of(polygonRefs.begin(), polygonRefs.end(), [&](uint32_t ringIndex) {
        return mesh.getPolygon(ringIndex).getVertexIndexSet() !=
               referenceVertices;
      })) {
    return false;
  }

  for (auto ringIndex : polygonRefs) {
    vector<wp::Vector2> healedPositions;
    for (auto v : mesh.getPolygon(ringIndex).getOrderedVertexIndices()) {
      if (v != vertexIndex) {
        healedPositions.push_back(mesh.getVertex(v).getPosition());
      }
    }
    if (!ringMutationIsValid(mesh, ringIndex, healedPositions)) {
      return false;
    }
  }

  if (polygonRefs.size() == 2) {
    uint32_t holeIndex = ~0u;
    uint32_t islandIndex = ~0u;
    for (auto ringIndex : polygonRefs) {
      (mesh.getPolygon(ringIndex).isHole() ? holeIndex : islandIndex) =
          ringIndex;
    }
    if (holeIndex != ~0u && islandIndex != ~0u) {
      // Mesh::removeVertex normally merges every face incident to a shared
      // vertex. Temporarily remove the island face, heal the remaining hole,
      // then recreate the island over the healed shared boundary. Its own
      // holes are detached and reattached without changing their geometry.
      auto islandHoles = mesh.getPolygon(islandIndex).getHoleIndices();
      mesh.removePolygon(islandIndex, false);
      mesh.removeVertex(vertexIndex);
      auto healedIsland = addRingSharingBoundary(mesh, holeIndex);
      for (auto islandHole : islandHoles) {
        mesh.addHoleToPolygon(healedIsland, islandHole);
      }
      return true;
    }
  }

  if (polygonRefs.size() > 1) {
    return false;
  }
  mesh.removeVertex(vertexIndex);
  return true;
}

// Welds an edge's two endpoints together at its midpoint by moving both
// there and then removing one of them, so the Ring heals through the same
// neighbour-joining path a vertex delete uses.
bool deleteOneMeshEdge(wp::geometry::Mesh& mesh, uint32_t edgeIndex, set<uint32_t>& consumedEdges) {
  if (consumedEdges.contains(edgeIndex)) {
    return false;
  }

  auto const& edge = mesh.getEdge(edgeIndex);
  auto ringIndex = owningRing(edge.getPolygonReferences());
  if (ringIndex == ~0u || mesh.getPolygon(ringIndex).getNumEdges() <= 3) {
    return false;
  }

  auto v0 = static_cast<uint32_t>(edge.getFirstVertex());
  auto v1 = static_cast<uint32_t>(edge.getSecondVertex());
  auto midpoint = (mesh.getVertex(v0).getPosition() + mesh.getVertex(v1).getPosition()) / 2.0f;

  // v0 is about to be removed - both of its edges (this one, and its other
  // neighbour) die with it, so anything else queued that names either must
  // be skipped rather than acted on again.
  auto v0EdgeRefs = mesh.getVertex(v0).getEdgeReferences();

  vector<wp::Vector2> weldedPositions;
  for (auto v : mesh.getPolygon(ringIndex).getOrderedVertexIndices()) {
    if (v == v0) {
      continue;
    }
    weldedPositions.push_back(v == v1 ? midpoint : mesh.getVertex(v).getPosition());
  }
  if (!ringMutationIsValid(mesh, ringIndex, weldedPositions)) {
    return false;
  }

  mesh.moveVertexTo(v0, midpoint);
  mesh.moveVertexTo(v1, midpoint);
  mesh.removeVertex(v0);
  consumedEdges.insert(v0EdgeRefs.begin(), v0EdgeRefs.end());
  return true;
}

// Removes a whole Ring: a hole is detached from its outer, an outer takes
// its holes with it. Ring deletion has no minimum-count rule and cannot
// make a surviving Ring self-intersect, so there is nothing to validate.
bool deleteOneMeshRing(wp::geometry::Mesh& mesh, uint32_t ringIndex, set<uint32_t>& consumedRings) {
  if (consumedRings.contains(ringIndex)) {
    return false;
  }

  auto const& polygon = mesh.getPolygon(ringIndex);
  if (polygon.isHole()) {
    for (auto outerIndex = mesh.getFirstPolygonIndex();
         !mesh.polygonIndexIterationFinished(outerIndex);
         outerIndex = mesh.getNextPolygonIndex(outerIndex)) {
      auto const& outerPolygon = mesh.getPolygon(outerIndex);
      auto const& holes = outerPolygon.getHoleIndices();
      if (!outerPolygon.isHole() && find(holes.begin(), holes.end(), ringIndex) != holes.end()) {
        mesh.removeHoleFromPolygon(outerIndex, ringIndex);
        break;
      }
    }
  } else {
    auto holeIndices = polygon.getHoleIndices();
    mesh.removePolygon(ringIndex, true);
    consumedRings.insert(holeIndices.begin(), holeIndices.end());
  }

  return true;
}

// Processes indices in ascending order (the set's natural iteration order),
// re-checking each item's minimum-count rule and shape invariants against
// the mesh as it stands after every prior removal in the same batch.
uint32_t simulateMeshSubObjectDeletion(
    wp::geometry::Mesh& mesh, Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  uint32_t removed = 0;
  if (subMode == Settings::MeshSubMode::Vertex) {
    for (auto vertexIndex : indices) {
      if (deleteOneMeshVertex(mesh, vertexIndex)) {
        ++removed;
      }
    }
  } else if (subMode == Settings::MeshSubMode::Edge) {
    set<uint32_t> consumedEdges;
    for (auto edgeIndex : indices) {
      if (deleteOneMeshEdge(mesh, edgeIndex, consumedEdges)) {
        ++removed;
      }
    }
  } else {
    set<uint32_t> consumedRings;
    for (auto ringIndex : indices) {
      if (deleteOneMeshRing(mesh, ringIndex, consumedRings)) {
        ++removed;
      }
    }
  }
  return removed;
}

}  // namespace

void Document::beginMeshDrag(Settings::MeshSubMode subMode) {
  mMeshDragStartSnapshot.reset();
  mMeshDragAffectedVertices.clear();
  mMeshDragAnchorVertexIndex = ~0u;
  mMeshDragAnchorStartPosition = {};
  mMeshDragLastValidDelta = {};

  if (!mActiveMesh) {
    return;
  }

  mMeshDragAffectedVertices = affectedMeshVertices(
      *mActiveMesh, subMode, getSelectedMeshSubObjectIndices(subMode));
  if (mMeshDragAffectedVertices.empty()) {
    return;
  }

  mMeshDragStartSnapshot = make_unique<wp::geometry::Mesh>(*mActiveMesh);
  mMeshDragAnchorVertexIndex = *mMeshDragAffectedVertices.begin();
  mMeshDragAnchorStartPosition =
      mActiveMesh->getVertex(mMeshDragAnchorVertexIndex).getPosition();
}

wp::Vector2 Document::updateMeshDrag(
    wp::Vector2 const& totalWorldDelta, bool snapToGrid, float gridSize) {
  if (!mActiveMesh || !mMeshDragStartSnapshot ||
      mMeshDragAffectedVertices.empty()) {
    return {};
  }

  auto delta = totalWorldDelta;
  if (snapToGrid && gridSize > 0.0f) {
    auto target = mMeshDragAnchorStartPosition + totalWorldDelta;
    wp::Vector2 snapped{
        std::round(target.x / gridSize) * gridSize,
        std::round(target.y / gridSize) * gridSize};
    delta = snapped - mMeshDragAnchorStartPosition;
  }

  wp::geometry::Mesh candidate(*mMeshDragStartSnapshot);
  wp::geometry::IndexVector indices(
      mMeshDragAffectedVertices.begin(), mMeshDragAffectedVertices.end());
  candidate.moveVertices(indices, delta);

  if (meshGroupMoveIsValid(
          candidate, *mMeshDragStartSnapshot, mMeshDragAffectedVertices, delta)) {
    mMeshDragLastValidDelta = delta;
    // Assigned in place, not replaced, so a pointer taken from
    // getActiveMesh() earlier in the same frame stays valid.
    *mActiveMesh = candidate;
  }

  return mMeshDragLastValidDelta;
}

void Document::endMeshDrag() {
  mMeshDragStartSnapshot.reset();
  mMeshDragAffectedVertices.clear();
  mMeshDragAnchorVertexIndex = ~0u;
  mMeshDragAnchorStartPosition = {};
  mMeshDragLastValidDelta = {};
}

bool Document::commitMeshDrag() {
  if (!mActiveMesh || mActiveMeshPrimitiveIndex == ~0u) {
    return false;
  }

  auto* primitive =
      static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(mActiveMeshPrimitiveIndex));
  primitive->updateFromGeometryProxy(*mActiveMesh);
  primitive->updateVertexPositions();
  return true;
}

bool Document::moveMeshVertexTo(uint32_t vertexIndex, wp::Vector2 const& position) {
  if (!mActiveMesh) {
    return false;
  }

  auto delta = position - mActiveMesh->getVertex(vertexIndex).getPosition();

  wp::geometry::MeshValidator validator(mActiveMesh.get());
  if (validator.validateVertexMove(vertexIndex, delta) !=
      wp::geometry::MeshValidator::Valid) {
    return false;
  }

  mActiveMesh->moveVertex(vertexIndex, delta);

  auto* primitive =
      static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(mActiveMeshPrimitiveIndex));
  primitive->updateFromGeometryProxy(*mActiveMesh);
  primitive->updateVertexPositions();
  return true;
}

uint32_t Document::previewMeshSubObjectDeletionCount(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) const {
  if (!mActiveMesh || indices.empty()) {
    return 0;
  }
  wp::geometry::Mesh candidate(*mActiveMesh);
  return simulateMeshSubObjectDeletion(candidate, subMode, indices);
}

uint32_t Document::deleteMeshSubObjects(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  if (!mActiveMesh || mActiveMeshPrimitiveIndex == ~0u || indices.empty()) {
    return 0;
  }

  wp::geometry::Mesh candidate(*mActiveMesh);
  auto removed = simulateMeshSubObjectDeletion(candidate, subMode, indices);
  if (removed == 0) {
    return 0;
  }

  uint32_t remainingRings = 0;
  for (auto index = candidate.getFirstPolygonIndex();
       !candidate.polygonIndexIterationFinished(index);
       index = candidate.getNextPolygonIndex(index)) {
    if (!candidate.getPolygon(index).isHole()) {
      ++remainingRings;
    }
  }

  auto primitiveIndex = mActiveMeshPrimitiveIndex;
  if (remainingRings == 0) {
    clearActiveMesh();
    mWorld->removePrimitives({primitiveIndex});
  } else {
    // Reindexes away the tombstones the deletes above left behind, so the
    // assignment below - a full Mesh copy - never has to rebind a dead edge.
    candidate.compact();
    *mActiveMesh = candidate;
    clearMeshSelections();
    auto* primitive =
        static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(primitiveIndex));
    primitive->updateFromGeometryProxy(*mActiveMesh);
    primitive->updateVertexPositions();
  }

  return removed;
}

uint32_t Document::splitMeshEdges(set<uint32_t> const& edgeIndices) {
  if (!mActiveMesh || mActiveMeshPrimitiveIndex == ~0u || edgeIndices.empty()) {
    return 0;
  }

  // Splitting only ever appends a new vertex and a new edge - it never
  // tombstones anything - so every original index in edgeIndices stays
  // valid for the rest of this loop, whatever order they're processed in.
  set<uint32_t> resultingEdges;
  uint32_t splitCount = 0;
  for (auto edgeIndex : edgeIndices) {
    wp::geometry::SplitEdgeResult result;
    wp::geometry::MeshOperations::splitEdge(mActiveMesh.get(), edgeIndex, 0.5f, &result);
    if (!result.newEdgeIndices.empty()) {
      ++splitCount;
    }
    resultingEdges.insert(result.newEdgeIndices.begin(), result.newEdgeIndices.end());
  }

  if (splitCount == 0) {
    return 0;
  }

  clearMeshSelections();
  mSelectedMeshEdgeIndices = resultingEdges;

  auto* primitive =
      static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(mActiveMeshPrimitiveIndex));
  primitive->updateFromGeometryProxy(*mActiveMesh);
  primitive->updateVertexPositions();

  return splitCount;
}

uint32_t Document::previewMeshEdgeSplitCount(set<uint32_t> const& edgeIndices) const {
  if (!mActiveMesh || edgeIndices.empty()) {
    return 0;
  }

  wp::geometry::Mesh candidate(*mActiveMesh);
  uint32_t splitCount = 0;
  for (auto edgeIndex : edgeIndices) {
    wp::geometry::SplitEdgeResult result;
    wp::geometry::MeshOperations::splitEdge(&candidate, edgeIndex, 0.5f, &result);
    if (!result.newEdgeIndices.empty()) {
      ++splitCount;
    }
  }

  return splitCount;
}

namespace {

float twiceSignedArea(vector<wp::Vector2> const& points) {
  float area = 0.0f;
  for (size_t i = 0; i < points.size(); ++i) {
    auto const& a = points[i];
    auto const& b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area;
}

float ringArea(wp::geometry::Mesh const& mesh, uint32_t polygonIndex) {
  vector<wp::Vector2> points;
  for (auto vertex : mesh.getPolygon(polygonIndex).getOrderedVertexIndices()) {
    points.push_back(mesh.getVertex(vertex).getPosition());
  }
  return abs(twiceSignedArea(points));
}

uint32_t innermostRingAt(
    wp::geometry::Mesh const& mesh, wp::Vector2 const& position) {
  uint32_t result = ~0u;
  float smallestArea = numeric_limits<float>::max();
  for (auto index = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(index);
       index = mesh.getNextPolygonIndex(index)) {
    auto area = ringArea(mesh, index);
    if (area < smallestArea && pointInsideRing(mesh, mesh.getPolygon(index), position)) {
      result = index;
      smallestArea = area;
    }
  }
  return result;
}

float orientation(wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointOnSegment(wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& p) {
  constexpr float epsilon = 0.0001f;
  return abs(orientation(a, b, p)) <= epsilon &&
         p.x >= min(a.x, b.x) - epsilon && p.x <= max(a.x, b.x) + epsilon &&
         p.y >= min(a.y, b.y) - epsilon && p.y <= max(a.y, b.y) + epsilon;
}

bool segmentsIntersect(
    wp::Vector2 const& a, wp::Vector2 const& b,
    wp::Vector2 const& c, wp::Vector2 const& d) {
  auto o1 = orientation(a, b, c);
  auto o2 = orientation(a, b, d);
  auto o3 = orientation(c, d, a);
  auto o4 = orientation(c, d, b);
  if (((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) &&
      ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f))) {
    return true;
  }
  return pointOnSegment(a, b, c) || pointOnSegment(a, b, d) ||
         pointOnSegment(c, d, a) || pointOnSegment(c, d, b);
}

bool segmentCrossesMesh(
    wp::geometry::Mesh const& mesh,
    wp::Vector2 const& first,
    wp::Vector2 const& second) {
  for (auto edgeIndex = mesh.getFirstEdgeIndex();
       !mesh.edgeIndexIterationFinished(edgeIndex);
       edgeIndex = mesh.getNextEdgeIndex(edgeIndex)) {
    auto const& edge = mesh.getEdge(edgeIndex);
    if (segmentsIntersect(
            first, second,
            mesh.getVertex(edge.getFirstVertex()).getPosition(),
            mesh.getVertex(edge.getSecondVertex()).getPosition())) {
      return true;
    }
  }
  return false;
}

bool segmentCrossesDrawnRing(
    vector<wp::Vector2> const& points,
    wp::Vector2 const& endpoint,
    bool closing) {
  if (points.size() < 2) {
    return false;
  }
  auto const& start = points.back();
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    // The candidate shares the last edge's start. A closing edge also shares
    // its endpoint with the first edge; those two adjacency touches are sound.
    if (i + 1 == points.size() - 1 || (closing && i == 0)) {
      continue;
    }
    if (segmentsIntersect(start, endpoint, points[i], points[i + 1])) {
      return true;
    }
  }
  return false;
}

uint32_t addDrawnRing(
    wp::geometry::Mesh& mesh, vector<wp::Vector2> const& points) {
  wp::geometry::IndexVector vertices;
  wp::geometry::IndexVector edgeData;
  for (auto const& point : points) {
    vertices.push_back(mesh.addVertex(wp::geometry::Vertex(point)));
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto first = vertices[i];
    auto second = vertices[(i + 1) % vertices.size()];
    auto edge = mesh.addEdge(wp::geometry::Edge(first, second));
    edgeData.insert(edgeData.end(), {first, second, edge});
  }
  return mesh.addPolygon(wp::geometry::Polygon(edgeData));
}

}  // namespace

string Document::meshDrawToolUnavailableReason(Settings const& settings) const {
  if (!isActive()) {
    return "No World is open.";
  }
  if (settings.mode != Settings::Mode::Mesh) {
    return "The draw tool is only available in Mesh mode.";
  }
  if (settings.meshSubMode != Settings::MeshSubMode::Vertex) {
    return "The draw tool is only available in Vertex sub-mode.";
  }

  auto* step = mWorld->getActiveLayer()->getActiveStep();
  if (auto* definePrefabs = dynamic_cast<bw::core::DefinePrefabs*>(step);
      definePrefabs && !definePrefabs->getSelectedPrefab()) {
    return "Select a Prefab first.";
  }
  if (!step->acceptsNewPrimitives()) {
    return "The selected LayerBuildStep does not accept new Primitives.";
  }
  if (!step->isEnabled()) {
    return "The selected LayerBuildStep is disabled.";
  }
  return {};
}

bool Document::armMeshDrawTool(Settings const& settings) {
  if (!meshDrawToolUnavailableReason(settings).empty()) {
    return false;
  }
  mMeshDrawToolArmed = true;
  mMeshDrawVertices.clear();
  mMeshDrawContainingRingIndex = ~0u;
  mMeshDrawContainingPrimitiveIndex = ~0u;
  mMeshDrawCreatesHole = false;
  mMeshDrawCreatesIsland = false;
  mMeshDrawRejection.clear();
  return true;
}

void Document::disarmMeshDrawTool() {
  mMeshDrawToolArmed = false;
  mMeshDrawVertices.clear();
  mMeshDrawContainingRingIndex = ~0u;
  mMeshDrawContainingPrimitiveIndex = ~0u;
  mMeshDrawCreatesHole = false;
  mMeshDrawCreatesIsland = false;
  mMeshDrawRejection.clear();
}

bool Document::meshDrawToolArmed() const {
  return mMeshDrawToolArmed;
}

vector<wp::Vector2> const& Document::getMeshDrawVertices() const {
  return mMeshDrawVertices;
}

uint32_t Document::getMeshDrawContainingRingIndex() const {
  return mMeshDrawContainingRingIndex;
}

bool Document::meshDrawCreatesNewPrimitive() const {
  return mMeshDrawContainingPrimitiveIndex == ~0u;
}

bool Document::meshDrawCreatesHole() const {
  return mMeshDrawCreatesHole;
}

bool Document::meshDrawCreatesIsland() const {
  return mMeshDrawCreatesIsland;
}

string const& Document::getMeshDrawRejection() const {
  return mMeshDrawRejection;
}

wp::Vector2 const& Document::getMeshDrawRejectedPosition() const {
  return mMeshDrawRejectedPosition;
}

wp::Vector2 Document::snapMeshDrawPosition(
    wp::Vector2 const& worldPosition, bool snapToGrid, float gridSize) {
  if (!snapToGrid || gridSize <= 0.0f) {
    return worldPosition;
  }
  return {
      std::round(worldPosition.x / gridSize) * gridSize,
      std::round(worldPosition.y / gridSize) * gridSize};
}

bool Document::meshDrawClickWouldClose(
    wp::Vector2 const& position, Settings const& settings) const {
  if (!mMeshDrawToolArmed || mMeshDrawVertices.size() < 3) {
    return false;
  }
  auto radiusSq = settings.meshVertexPickRadius * settings.meshVertexPickRadius;
  if (mMeshDrawVertices.front().distanceToSq(position) > radiusSq) {
    return false;
  }
  auto const& endpoint = mMeshDrawVertices.front();
  if (segmentCrossesDrawnRing(mMeshDrawVertices, endpoint, true)) {
    return false;
  }
  return !mActiveMesh || mMeshDrawContainingRingIndex == ~0u ||
         !segmentCrossesMesh(*mActiveMesh, mMeshDrawVertices.back(), endpoint);
}

bool Document::placeMeshDrawVertex(
    wp::Vector2 const& position, Settings const& settings) {
  if (!mMeshDrawToolArmed) {
    return false;
  }

  auto reject = [&](string reason) {
    mMeshDrawRejection = move(reason);
    mMeshDrawRejectedPosition = position;
    return false;
  };

  auto radiusSq = settings.meshVertexPickRadius * settings.meshVertexPickRadius;
  for (size_t i = 0; i < mMeshDrawVertices.size(); ++i) {
    if (mMeshDrawVertices[i].distanceToSq(position) <= radiusSq) {
      if (i == 0 && mMeshDrawVertices.size() >= 3) {
        return reject("Rejected: the closing edge would cross the Ring or its containing boundary.");
      }
      return reject("Rejected: a vertex is already placed there.");
    }
  }

  if (mMeshDrawVertices.empty()) {
    // Exact picking finds filled regions. A point in a hole is not an exact
    // hit, so make a second pass over MeshPrimitive Rings to recover that
    // authoring context. The first geometrically-containing Primitive wins,
    // matching the World's ordinary front-to-back index order.
    auto primitiveIndex = getPrimitiveIndexAt(position);
    if (primitiveIndex == ~0u) {
      for (uint32_t i = 0; i < mWorld->getNumPrimitives(); ++i) {
        auto* primitive = dynamic_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(i));
        if (!primitive || primitive->hasFlag(BW_PRIMITIVE_GHOST_FLAG)) {
          continue;
        }
        auto proxy = i == mActiveMeshPrimitiveIndex && mActiveMesh
                         ? nullptr
                         : primitive->createGeometryProxy();
        auto const& candidate = proxy ? *proxy : *mActiveMesh;
        if (innermostRingAt(candidate, position) != ~0u) {
          primitiveIndex = i;
          break;
        }
      }
    }

    if (primitiveIndex != ~0u && meshIneligibilityReason(primitiveIndex).empty() &&
        activateMesh(primitiveIndex)) {
      auto ring = innermostRingAt(*mActiveMesh, position);
      if (ring != ~0u) {
        mMeshDrawContainingPrimitiveIndex = primitiveIndex;
        mMeshDrawContainingRingIndex = ring;
        mMeshDrawCreatesIsland = mActiveMesh->getPolygon(ring).isHole();
        mMeshDrawCreatesHole = !mMeshDrawCreatesIsland;
      }
    } else {
      clearActiveMesh();
    }
  } else {
    if (segmentCrossesDrawnRing(mMeshDrawVertices, position, false)) {
      return reject("Rejected: that edge would make the Ring cross itself.");
    }
    if (mMeshDrawContainingRingIndex != ~0u) {
      if (!mActiveMesh ||
          innermostRingAt(*mActiveMesh, position) != mMeshDrawContainingRingIndex ||
          segmentCrossesMesh(*mActiveMesh, mMeshDrawVertices.back(), position)) {
        return reject("Rejected: that vertex would leave the containing region.");
      }
    }
  }

  mMeshDrawRejection.clear();
  mMeshDrawVertices.push_back(position);
  return true;
}

bool Document::removeLastMeshDrawVertex() {
  if (!mMeshDrawToolArmed || mMeshDrawVertices.empty()) {
    return false;
  }
  mMeshDrawVertices.pop_back();
  mMeshDrawRejection.clear();
  if (mMeshDrawVertices.empty()) {
    mMeshDrawContainingRingIndex = ~0u;
    mMeshDrawContainingPrimitiveIndex = ~0u;
    mMeshDrawCreatesHole = false;
    mMeshDrawCreatesIsland = false;
  }
  return true;
}

bool Document::escapeMeshDraw() {
  if (!mMeshDrawToolArmed) {
    return false;
  }
  if (!mMeshDrawVertices.empty()) {
    mMeshDrawVertices.clear();
    mMeshDrawContainingRingIndex = ~0u;
    mMeshDrawContainingPrimitiveIndex = ~0u;
    mMeshDrawCreatesHole = false;
    mMeshDrawCreatesIsland = false;
    mMeshDrawRejection.clear();
    return true;
  }
  mMeshDrawToolArmed = false;
  return true;
}

bool Document::fillMeshHole(uint32_t holeRingIndex) {
  if (!mActiveMesh || mActiveMeshPrimitiveIndex == ~0u) {
    return false;
  }

  bool found = false;
  for (auto index = mActiveMesh->getFirstPolygonIndex();
       !mActiveMesh->polygonIndexIterationFinished(index);
       index = mActiveMesh->getNextPolygonIndex(index)) {
    if (index == holeRingIndex) {
      found = true;
      break;
    }
  }
  if (!found || !mActiveMesh->getPolygon(holeRingIndex).isHole()) {
    return false;
  }

  // Find the hole's immediate filled islands before adding anything. A
  // deeper island belongs to the smallest hole containing it and is already
  // excluded transitively by its ancestor island.
  vector<uint32_t> immediateIslands;
  for (auto candidateIndex = mActiveMesh->getFirstPolygonIndex();
       !mActiveMesh->polygonIndexIterationFinished(candidateIndex);
       candidateIndex = mActiveMesh->getNextPolygonIndex(candidateIndex)) {
    auto const& candidate = mActiveMesh->getPolygon(candidateIndex);
    if (candidate.isHole()) {
      continue;
    }
    auto ordered = candidate.getOrderedVertexIndices();
    if (ordered.empty()) {
      continue;
    }
    auto sample = mActiveMesh->getVertex(ordered.front()).getPosition();
    uint32_t smallestContainingHole = ~0u;
    float smallestArea = numeric_limits<float>::max();
    for (auto containingIndex = mActiveMesh->getFirstPolygonIndex();
         !mActiveMesh->polygonIndexIterationFinished(containingIndex);
         containingIndex = mActiveMesh->getNextPolygonIndex(containingIndex)) {
      auto const& containing = mActiveMesh->getPolygon(containingIndex);
      auto area = ringArea(*mActiveMesh, containingIndex);
      if (containing.isHole() && area < smallestArea &&
          pointInsideRing(*mActiveMesh, containing, sample)) {
        smallestContainingHole = containingIndex;
        smallestArea = area;
      }
    }
    if (smallestContainingHole == holeRingIndex) {
      immediateIslands.push_back(candidateIndex);
    }
  }

  // The gap is a new Polygon face bounded by the selected hole's existing
  // edges, not a second coincident set of vertices and edges.
  auto filledRingIndex = addRingSharingBoundary(*mActiveMesh, holeRingIndex);

  // Fill only the gap. Existing islands remain independent top-level solids,
  // while new hole loops share their boundary edges with those islands. Thus
  // the pieces meet without overlap, and deleting this polygon restores the
  // exact previous hole/island topology.
  for (auto islandIndex : immediateIslands) {
    auto gapHoleIndex = addRingSharingBoundary(*mActiveMesh, islandIndex);
    mActiveMesh->addHoleToPolygon(filledRingIndex, gapHoleIndex);
  }

  auto* primitive = static_cast<bw::core::MeshPrimitive*>(
      mWorld->getPrimitive(mActiveMeshPrimitiveIndex));
  primitive->updateFromGeometryProxy(*mActiveMesh);
  primitive->updateVertexPositions();
  clearMeshSelections();
  mSelectedMeshRingIndices.insert(filledRingIndex);
  return true;
}

bw::core::Primitive* Document::closeMeshDrawRing() {
  if (!mMeshDrawToolArmed || mMeshDrawVertices.size() < 3 || !isActive()) {
    return nullptr;
  }

  auto* step = mWorld->getActiveLayer()->getActiveStep();
  if (!step->acceptsNewPrimitives() || !step->isEnabled()) {
    return nullptr;
  }

  // Canonical winding is anticlockwise, which is what the arrangement's own
  // outer boundaries carry and therefore what a baked MeshPrimitive already
  // has: a drawn Ring is then indistinguishable from a baked one downstream.
  auto points = mMeshDrawVertices;
  if (twiceSignedArea(points) < 0.0f) {
    reverse(points.begin(), points.end());
  }

  bw::core::ClosedPolygon ring;
  for (auto const& point : points) {
    ring.emplace_back(point);
  }

  // The Create Primitive panel is hidden in Mesh mode, so the ghost carries
  // the settings for what is about to be created and the Mesh panel writes
  // through to it.
  if (mMeshDrawContainingPrimitiveIndex != ~0u && mActiveMesh) {
    auto* primitive = static_cast<bw::core::MeshPrimitive*>(
        mWorld->getPrimitive(mMeshDrawContainingPrimitiveIndex));
    auto newRing = addDrawnRing(*mActiveMesh, points);
    if (mMeshDrawCreatesHole) {
      mActiveMesh->addHoleToPolygon(mMeshDrawContainingRingIndex, newRing);
    }
    // Commit topology to the authored Primitive before the action requests
    // asynchronous regeneration; the proxy alone is never generator input.
    primitive->updateFromGeometryProxy(*mActiveMesh);
    primitive->updateVertexPositions();
    disarmMeshDrawTool();
    clearSelections();
    return primitive;
  }

  auto* ghost = getGhost();
  auto* mesh = bw::core::MeshPrimitive::fromComplexPolygons(
      ghost->getOperation(), ghost->getFillRule(), {{ring}});
  mesh->setPriority(ghost->getPriority());

  disarmMeshDrawTool();
  clearSelections();
  activateMesh(mWorld->addPrimitive(mesh));
  return mesh;
}

bool Document::recentreActiveMesh() {
  if (!mActiveMesh || mActiveMeshPrimitiveIndex == ~0u) {
    return false;
  }

  wp::Vector2 minExtent, maxExtent;
  mActiveMesh->getExtents(minExtent, maxExtent);
  auto centre = (minExtent + maxExtent) * 0.5f;
  auto halfSize = (maxExtent - minExtent) * 0.5f;
  auto scale = std::max(halfSize.x, halfSize.y);
  if (scale <= 0.0f) {
    return false;
  }

  auto* primitive =
      static_cast<bw::core::MeshPrimitive*>(mWorld->getPrimitive(mActiveMeshPrimitiveIndex));
  primitive->setPosition(centre);
  primitive->setSize(scale * 2.0f, scale * 2.0f);
  primitive->updateFromGeometryProxy(*mActiveMesh);
  primitive->updateVertexPositions();
  return true;
}

vector<uint32_t> Document::getPrimitiveIndicesInBounds(wp::BoundingBox const& worldBounds, Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  // The ghost is authoring furniture, never a selectable object, so it is
  // excluded here unconditionally rather than only when settings.ghostActive
  // is off (which governs whether it is drawn and hoverable, not this).
  auto ignores = getIgnoredPrimitiveIndices(*mWorld, settings);
  ignores.insert(uint32_t(ED_GHOST_INDEX));

  vector<uint32_t> result;
  auto numPrimitives = mWorld->getNumPrimitives();

  for (uint32_t index = 0; index < numPrimitives; ++index) {
    if (ignores.find(index) != ignores.end()) {
      continue;
    }

    auto primitive = mWorld->getPrimitive(index);

    if (worldBounds.intersectsBoundingObject(&primitive->getBounds())) {
      result.push_back(index);
    }
  }

  return result;
}

vector<uint32_t> Document::getSelectablePrimitiveIndices(Settings const& settings) const {
  if (!isActive()) {
    return {};
  }

  // See the equivalent comment in getPrimitiveIndicesInBounds: the ghost is
  // excluded here unconditionally, not just when settings.ghostActive is off.
  auto ignores = getIgnoredPrimitiveIndices(*mWorld, settings);
  ignores.insert(uint32_t(ED_GHOST_INDEX));

  vector<uint32_t> result;
  auto numPrimitives = mWorld->getNumPrimitives();

  for (uint32_t index = 0; index < numPrimitives; ++index) {
    if (ignores.find(index) == ignores.end()) {
      result.push_back(index);
    }
  }

  return result;
}

uint32_t Document::getHoveredTriggerLineIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const {
  return isActive() && settings.mode != Settings::Mode::Mesh
             ? mWorld->findTriggerLineIndex(
                   mouseWorldPos, settings.triggerLineSelectionDistance,
                   settings.triggerLineHandleRadius)
             : ~0u;
}

bool Document::indexInSelection(uint32_t index) const {
  return mSelectedPrimitiveIndices.find(index) != mSelectedPrimitiveIndices.end();
}

set<uint32_t> const& Document::getSelectedPrimitiveIndices() const {
  return mSelectedPrimitiveIndices;
}

bool Document::anyPrimitiveIndicesSelected(vector<uint32_t> const& indices) const {
  for (auto index : indices) {
    if (find(mSelectedPrimitiveIndices.begin(), mSelectedPrimitiveIndices.end(), index) != mSelectedPrimitiveIndices.end()) {
      return true;
    }
  }

  return false;
}

uint32_t Document::getSelectedWorldVertexIndex() const {
  return mSelectedWorldVertexIndex;
}

uint32_t Document::getSelectedTriggerLineIndex() const {
  return mSelectedTriggerLineIndex;
}

bool Document::hasSelection() const {
  return !mSelectedPrimitiveIndices.empty() || mSelectedTriggerLineIndex != ~0u ||
         mSelectedWorldVertexIndex != ~0u || !mSelectedMeshVertexIndices.empty() ||
         !mSelectedMeshEdgeIndices.empty() || !mSelectedMeshRingIndices.empty();
}

void Document::setPlayerProxyPosition(wp::Vector2 const& pos) {
  mPlayerOldProxyPosition = mPlayerProxyPosition;
  mPlayerProxyPosition = pos;
}

wp::Vector2 const& Document::getPlayerProxyPosition() const {
  return mPlayerProxyPosition;
}

wp::Vector2 const& Document::getPlayerOldProxyPosition() const {
  return mPlayerOldProxyPosition;
}

void Document::setPlayerProxyAngle(float angle) {
  mPlayerOldProxyAngle = mPlayerProxyAngle;
  mPlayerProxyAngle = angle;
}

float Document::getPlayerProxyAngle() const {
  return mPlayerProxyAngle;
}

float Document::getPlayerOldProxyAngle() const {
  return mPlayerOldProxyAngle;
}

bw::core::Primitive* Document::getGhost() {
  if (isActive()) {
    return mWorld->getPrimitive(0);
  } else {
    throw EditorException("Document not active");
  }
}

void Document::updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive) {
  primitive->setFlags(primitive->getFlags() | BW_PRIMITIVE_GHOST_FLAG);

  if (world->getNumPrimitives() == 0) {
    world->addPrimitive(primitive);
  } else {
    world->replacePrimitive(0, primitive);
  }
}

std::shared_ptr<bw::core::World> Document::createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto generator = new bw::core::DynamicWorldDataGenerator(world.get());
  generator->setAlwaysUpdateVertices(true);
  generator->setAllowCommitIfVisible(true);
  generator->setPrimitiveFilter(mPrimitiveFilter);
  world->setWorldDataGenerator(generator);

  // Create ghost primitive as a preview for creating primitives
  auto ghost = new bw::core::RegularPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      3);

  ghost->setPriority(0);
  ghost->setPosition(wp::Vector2::ZERO);

  {
    auto mutation = ghost->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale).setPoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::Angle).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitAngle).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance).setPoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
  }

  updateGhost(world, ghost);

  return world;
}

void Document::newDoc() {
  reset();

  mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
  mModified = false;
}

void Document::closeDoc() {
  reset();
}

bool Document::openDoc(string const& filepath) {
  reset();

  mFilepath = filepath;

  auto path = filesystem::path(mFilepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".yaml" || ext == ".world") {
    shared_ptr<bw::core::Serializer> ser = ext == ".yaml"
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(mFilepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(mFilepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      return false;
    }

    mWorld = createWorld(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);

    auto workData = bw::core::SerializationWorkData{};

    if (mWorld->deserialize(ser, workData)) {
      auto const& warnings = mWorld->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          gLogger->warn(warning);
        }
      }

      // Add the ghost back into the grids after they have been recreated
      mWorld->replacePrimitive(ED_GHOST_INDEX, mWorld->getPrimitive(ED_GHOST_INDEX), false);
      return true;
    } else {
      auto const& errors = mWorld->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          gLogger->error(error);
        }
      }

      reset();
      return false;
    }
  } else {
    throw EditorException(format("Could not open {} (filetype not supported)", mFilepath));
  }
}

void Document::saveDoc() {
  if (mFilepath == "") {
    throw EditorException("Document has no filepath set.");
  }

  auto path = std::filesystem::path(mFilepath);
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".yaml" || ext == ".world") {
    shared_ptr<bw::core::Serializer> ser = ext == ".yaml"
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toFile(mFilepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::toFile(mFilepath));
    auto workData = bw::core::SerializationWorkData{};

    mWorld->serialize(ser, workData);
    ser->serialize();
  } else {
    throw EditorException(format("Could not save {} (filetype not supported)", mFilepath));
  }

  mModified = false;
}

void Document::saveDocAs(string const& filepath) {
  mFilepath = filepath;
  saveDoc();
}

namespace {
// ".layer.yaml" is a distinct extension from ".yaml" - own weight, own
// dispatch here - not filesystem::path::extension(), which only ever sees
// the last dot-segment and would report ".yaml" for both.
bool hasExtension(string const& filepath, string const& extension) {
  if (filepath.size() < extension.size()) {
    return false;
  }

  auto const tail = filepath.substr(filepath.size() - extension.size());
  return equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return tolower(static_cast<unsigned char>(a)) == tolower(static_cast<unsigned char>(b));
  });
}
}  // namespace

void Document::exportLayer(bw::core::Layer const* layer, string const& filepath) const {
  shared_ptr<bw::core::Serializer> ser;

  if (hasExtension(filepath, ".layer.yaml")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toFile(filepath));
  } else if (hasExtension(filepath, ".layer")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::toFile(filepath));
  } else {
    throw EditorException(format("Could not export {} (filetype not supported)", filepath));
  }

  auto workData = bw::core::SerializationWorkData{};
  layer->serialize(ser, workData);
  ser->serialize();
}

bw::core::Layer* Document::importLayer(string const& filepath) {
  shared_ptr<bw::core::Serializer> ser;

  if (hasExtension(filepath, ".layer.yaml")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(filepath));
  } else if (hasExtension(filepath, ".layer")) {
    ser = shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(filepath));
  } else {
    throw EditorException(format("Could not import {} (filetype not supported)", filepath));
  }

  try {
    ser->deserialize();
  } catch (exception& e) {
    gLogger->error(e.what());
    return nullptr;
  }

  auto layer = make_unique<bw::core::Layer>();

  auto workData = bw::core::SerializationWorkData{};
  workData.accelGridSize = mWorld->getPrimitiveAccelerationGridSize();

  if (!layer->deserialize(ser, workData)) {
    for (auto const& error : layer->getDeserializationErrors()) {
      gLogger->error(error);
    }
    return nullptr;
  }

  for (auto const& warning : layer->getDeserializationWarnings()) {
    gLogger->warn(warning);
  }

  return mWorld->addLayer(layer.release());
}

}  // namespace editor