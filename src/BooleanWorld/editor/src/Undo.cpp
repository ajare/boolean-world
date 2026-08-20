#include <deque>
#include <set>
#include <ranges>

#include <common/BoundedDeque.h>

#include <core/World.h>

#include "Undo.h"
#include "Document.h"
#include "EditorException.h"
#include "UiHelpers.h"
#include "Settings.h"

#define MAX_STACK_SIZE 20

extern editor::Settings gEditorSettings;

namespace editor {
using namespace std;

struct UndoData {
  WorldSnapshot world;
  set<uint32_t> selection;
  uint32_t selectedWorldVertex{~0u};
  uint32_t selectedTriggerLine{~0u};
  uint32_t activeMeshPrimitive{~0u};
  set<uint32_t> selectedMeshVertices;
  set<uint32_t> selectedMeshEdges;
  set<uint32_t> selectedMeshRings;
  bool docModified{false};
};

struct UndoEntry {
  std::string id;
  UndoData data;
};

// Internal
static std::deque<UndoEntry> gUndoStack, gRedoStack;
static UndoData gTransactionalData;
static std::string gTransactionalId;
static float gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
static wp::Vector2 gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};
static UndoableActionFunction gTransactionalFunc;

UndoData captureUndoData(Document* doc) {
  return {
      doc->captureWorldSnapshot(),
      doc->getSelectedPrimitiveIndices(),
      doc->getSelectedWorldVertexIndex(),
      doc->getSelectedTriggerLineIndex(),
      doc->getActiveMeshPrimitiveIndex(),
      doc->getSelectedMeshVertexIndices(),
      doc->getSelectedMeshEdgeIndices(),
      doc->getSelectedMeshRingIndices(),
      doc->isModified()};
}

void restoreUndoData(Document* doc, UndoData const& data) {
  doc->restoreWorldSnapshot(data.world);
  doc->restoreMeshSelection(
      data.activeMeshPrimitive, data.selectedMeshVertices,
      data.selectedMeshEdges, data.selectedMeshRings);
  if (!data.selection.empty()) {
    doc->setSelectedPrimitiveIndices(data.selection);
  } else if (data.selectedTriggerLine != ~0u) {
    doc->setSelectedTriggerLineIndex(data.selectedTriggerLine);
  } else if (data.selectedWorldVertex != ~0u) {
    doc->setSelectedWorldVertexIndex(data.selectedWorldVertex);
  } else if (data.selectedMeshVertices.empty() && data.selectedMeshEdges.empty() &&
             data.selectedMeshRings.empty()) {
    doc->clearSelections();
  }
  doc->setModified(data.docModified);
}

bool canUndo() {
  return !gUndoStack.empty();
}

bool canRedo() {
  return !gRedoStack.empty();
}

size_t getUndoLevels() {
  return gUndoStack.size();
}

size_t getRedoLevels() {
  return gRedoStack.size();
}

void beginUndoableAction(Document* doc, string const& id, UndoableActionFunction func, float v) {
  if (gTransactionalFunc) {
    commitUndoableAction(doc);
    //	throw EditorException(format("Tried to begin action '{}' but '{}' was already in a transactional state.", id, gTransactionalId));
  }

  gTransactionalId = id;
  gTransactionalData = captureUndoData(doc);

  gTransactionalInitialFloatValue = v;
  gTransactionalFunc = func;
}

void beginUndoableAction(Document* doc, string const& id, UndoableActionFunction func, wp::Vector2 const& v) {
  if (gTransactionalFunc) {
    commitUndoableAction(doc);
    //	throw EditorException(format("Tried to begin action '{}' but '{}' was already in a transactional state.", id, gTransactionalId));
  }

  gTransactionalId = id;
  gTransactionalData = captureUndoData(doc);

  gTransactionalInitialVectorValue = v;
  gTransactionalFunc = func;
}

bool transactionValueHasChanged(float v) {
  return !isnan(gTransactionalInitialFloatValue) && gTransactionalInitialFloatValue != v;
}

bool transactionValueHasChanged(wp::Vector2 const& v) {
  return !isnan(gTransactionalInitialVectorValue.x) && gTransactionalInitialVectorValue != v;
}

void commitUndoableAction(Document* doc, string const& id) {
  gRedoStack.clear();

  UndoEntry data{id != "" ? id : gTransactionalId, gTransactionalData};
  gUndoStack.push_back(data);
  bw::common::trimDequeToCapacity(gUndoStack, MAX_STACK_SIZE);

  if (gTransactionalFunc(doc)) {
    doc->setModified();
  }

  gTransactionalFunc = nullptr;
  gTransactionalId.clear();
  gTransactionalData = {};
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};

  // The arrangement and everything drawn from it are derived from the World,
  // so an action that changed the World has just made them stale. This is the
  // one place that needs to say so: every action commits through here.
  //
  // Regeneration is asynchronous, and a request that has not started is
  // replaced by the next one, while one already running has its result
  // discarded - so a run of quick edits collapses onto the last of them.
  regenerateWorldData(doc);
}

void transactUndoableAction(Document* doc, string const& id, UndoableActionFunction func) {
  beginUndoableAction(doc, id, func, numeric_limits<float>::quiet_NaN());
  commitUndoableAction(doc);
}

bool transactUndoableActionAtomically(
    Document* doc,
    string const& id,
    UndoableActionFunction func) {
  if (gTransactionalFunc) {
    throw EditorException(
        "Cannot run an atomic action while another undoable action is in progress.");
  }

  auto previous = captureUndoData(doc);
  UndoEntry entry{id, previous};
  auto restorePrevious = [&]() { restoreUndoData(doc, previous); };

  try {
    if (!func(doc)) {
      restorePrevious();
      return false;
    }

    gUndoStack.push_back(move(entry));
    bw::common::trimDequeToCapacity(gUndoStack, MAX_STACK_SIZE);
    gRedoStack.clear();
    doc->setModified();
    regenerateWorldData(doc);
    return true;
  } catch (...) {
    restorePrevious();
    throw;
  }
}

void abandonUndoableAction(Document* doc) {
  gTransactionalId.clear();

  gTransactionalData = {};
  gTransactionalData.docModified = doc->isModified();
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};

  gTransactionalFunc = nullptr;
}

bool undoableActionInProgress() {
  return static_cast<bool>(gTransactionalFunc);
}

void undo(Document* doc, int count) {
  if (count <= 0 || !canUndo()) {
    generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
    return;
  }

  auto data = captureUndoData(doc);
  bool restored{false};

  for (int i = 0; i < count && canUndo(); ++i) {
    auto& oldEntry = gUndoStack.back();
    auto id = oldEntry.id;

    gRedoStack.push_back({move(id), move(data)});
    data = move(oldEntry.data);
    gUndoStack.pop_back();
    restored = true;
  }

  if (restored) {
    restoreUndoData(doc, data);
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
}

void redo(Document* doc, int count) {
  if (count <= 0 || !canRedo()) {
    generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
    return;
  }

  auto data = captureUndoData(doc);
  bool restored{false};

  for (int i = 0; i < count && canRedo(); ++i) {
    auto& oldEntry = gRedoStack.back();
    auto id = oldEntry.id;

    gUndoStack.push_back({move(id), move(data)});
    data = move(oldEntry.data);
    gRedoStack.pop_back();
    restored = true;
  }

  if (restored) {
    restoreUndoData(doc, data);
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
}

vector<HistoryItem> getActionHistory() {
  vector<HistoryItem> entries;

  for (auto const& item : gUndoStack) {
    entries.push_back({item.id, true});
  }

  ranges::reverse_view redoStackRev{gRedoStack};
  for (auto const& item : redoStackRev) {
    entries.push_back({item.id, false});
  }

  return entries;
}

}  // namespace editor