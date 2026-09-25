#include "Selection.h"

#include <algorithm>
#include <utility>

#include "core/World.h"

namespace editor {
using namespace std;

void Selection::setSelectedWorldVertexIndex(uint32_t index) {
  clearSelections();
  mSelectedWorldVertexIndex = index;
}

void Selection::setSelectedTriggerLineIndex(uint32_t index) {
  clearSelections();
  mSelectedTriggerLineIndex = index;
}

void Selection::setSelectedPortalEndpoint(
    uint32_t layerId, uint32_t loopId, uint32_t endpointId) {
  clearSelections();
  mSelectedPortalLayerId = layerId;
  mSelectedPortalLoopId = loopId;
  mSelectedPortalEndpointId = endpointId;
}

void Selection::setSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  clearSelections();
  mSelectedPrimitiveIndices = indices;
}

void Selection::addSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.insert(index);
}

void Selection::addSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  mSelectedPrimitiveIndices.insert(indices.begin(), indices.end());
}

void Selection::removeSelectedPrimitiveIndex(uint32_t index) {
  mSelectedPrimitiveIndices.erase(index);
}

void Selection::removeSelectedPrimitiveIndices(set<uint32_t> const& indices) {
  for (auto index : indices) mSelectedPrimitiveIndices.erase(index);
}

void Selection::clearSelections() {
  mSelectedPrimitiveIndices.clear();
  mSelectedWorldVertexIndex = ~0u;
  mSelectedTriggerLineIndex = ~0u;
  mSelectedPortalLayerId = ~0u;
  mSelectedPortalLoopId = ~0u;
  mSelectedPortalEndpointId = ~0u;
  clearMeshSelections();
}

void Selection::clearMeshSelections() {
  mSelectedMeshVertexIndices.clear();
  mSelectedMeshEdgeIndices.clear();
  mSelectedMeshRingIndices.clear();
}

void Selection::revalidateSelection() {
  auto const* world = selectionWorld();
  if (!world) return;

  auto numPrimitives = world->getNumPrimitives();
  for (auto it = mSelectedPrimitiveIndices.begin();
       it != mSelectedPrimitiveIndices.end();) {
    if (*it >= numPrimitives) it = mSelectedPrimitiveIndices.erase(it);
    else ++it;
  }
  if (mSelectedTriggerLineIndex != ~0u &&
      mSelectedTriggerLineIndex >= world->getNumTriggerLines()) {
    mSelectedTriggerLineIndex = ~0u;
  }
  if (mSelectedPortalLoopId != ~0u) {
    auto const* layer = world->getLayer(mSelectedPortalLayerId);
    auto const* portalLoop = layer
                           ? layer->getPortalLoop(mSelectedPortalLoopId)
                           : nullptr;
    if (!portalLoop || !portalLoop->findEndpoint(mSelectedPortalEndpointId)) {
      mSelectedPortalLayerId = ~0u;
      mSelectedPortalLoopId = ~0u;
      mSelectedPortalEndpointId = ~0u;
    }
  }
  // World vertices are regenerated asynchronously and have no synchronous
  // bound here. Their index is only compared with the sentinel.
}

set<uint32_t> const& Selection::getSelectedPrimitiveIndices() const {
  return mSelectedPrimitiveIndices;
}

bool Selection::indexInSelection(uint32_t index) const {
  return mSelectedPrimitiveIndices.contains(index);
}

bool Selection::anyPrimitiveIndicesSelected(vector<uint32_t> const& indices) const {
  return any_of(indices.begin(), indices.end(),
                [&](auto index) { return mSelectedPrimitiveIndices.contains(index); });
}

uint32_t Selection::getSelectedWorldVertexIndex() const {
  return mSelectedWorldVertexIndex;
}

uint32_t Selection::getSelectedTriggerLineIndex() const {
  return mSelectedTriggerLineIndex;
}

uint32_t Selection::getSelectedPortalLayerId() const {
  return mSelectedPortalLayerId;
}

uint32_t Selection::getSelectedPortalLoopId() const {
  return mSelectedPortalLoopId;
}

uint32_t Selection::getSelectedPortalEndpointId() const {
  return mSelectedPortalEndpointId;
}

bool Selection::hasSelectedPortalEndpoint() const {
  return mSelectedPortalLoopId != ~0u;
}

bool Selection::hasSelection() const {
  return !mSelectedPrimitiveIndices.empty() || mSelectedTriggerLineIndex != ~0u ||
         mSelectedPortalLoopId != ~0u ||
         mSelectedWorldVertexIndex != ~0u || !mSelectedMeshVertexIndices.empty() ||
         !mSelectedMeshEdgeIndices.empty() || !mSelectedMeshRingIndices.empty();
}

set<uint32_t> const& Selection::getSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode) const {
  if (subMode == Settings::MeshSubMode::Vertex) return mSelectedMeshVertexIndices;
  if (subMode == Settings::MeshSubMode::Edge) return mSelectedMeshEdgeIndices;
  return mSelectedMeshRingIndices;
}

set<uint32_t> const& Selection::getSelectedMeshVertexIndices() const {
  return mSelectedMeshVertexIndices;
}
set<uint32_t> const& Selection::getSelectedMeshEdgeIndices() const {
  return mSelectedMeshEdgeIndices;
}
set<uint32_t> const& Selection::getSelectedMeshRingIndices() const {
  return mSelectedMeshRingIndices;
}

void Selection::setSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  clearSelections();
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  *selection = indices;
}

void Selection::addSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  selection->insert(indices.begin(), indices.end());
}

void Selection::toggleSelectedMeshSubObjectIndices(
    Settings::MeshSubMode subMode, set<uint32_t> const& indices) {
  auto* selection = subMode == Settings::MeshSubMode::Vertex ? &mSelectedMeshVertexIndices
                    : subMode == Settings::MeshSubMode::Edge ? &mSelectedMeshEdgeIndices
                                                             : &mSelectedMeshRingIndices;
  for (auto index : indices) {
    if (!selection->erase(index)) selection->insert(index);
  }
}

void Selection::setMeshHoverExplanation(string explanation) {
  mMeshHoverExplanation = move(explanation);
}

string const& Selection::getMeshHoverExplanation() const {
  return mMeshHoverExplanation;
}

}  // namespace editor
