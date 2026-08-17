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

  gTransactionalData.world = doc->captureWorldSnapshot();
  gTransactionalData.selection = doc->getSelectedPrimitiveIndices();
  gTransactionalData.docModified = doc->isModified();

  gTransactionalInitialFloatValue = v;
  gTransactionalFunc = func;
}

void beginUndoableAction(Document* doc, string const& id, UndoableActionFunction func, wp::Vector2 const& v) {
  if (gTransactionalFunc) {
    commitUndoableAction(doc);
    //	throw EditorException(format("Tried to begin action '{}' but '{}' was already in a transactional state.", id, gTransactionalId));
  }

  gTransactionalId = id;

  gTransactionalData.world = doc->captureWorldSnapshot();
  gTransactionalData.selection = doc->getSelectedPrimitiveIndices();
  gTransactionalData.docModified = doc->isModified();

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
  gTransactionalData.world = {};
  gTransactionalData.selection.clear();
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};
}

void transactUndoableAction(Document* doc, string const& id, UndoableActionFunction func) {
  beginUndoableAction(doc, id, func, numeric_limits<float>::quiet_NaN());
  commitUndoableAction(doc);
}

void abandonUndoableAction(Document* doc) {
  gTransactionalId.clear();

  gTransactionalData.world = {};
  gTransactionalData.selection.clear();
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

  auto data = UndoData{
      doc->captureWorldSnapshot(),
      doc->getSelectedPrimitiveIndices(),
      doc->isModified()};
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
    doc->restoreWorldSnapshot(data.world);
    doc->setSelectedPrimitiveIndices(data.selection);
    doc->setModified(data.docModified);
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
}

void redo(Document* doc, int count) {
  if (count <= 0 || !canRedo()) {
    generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
    return;
  }

  auto data = UndoData{
      doc->captureWorldSnapshot(),
      doc->getSelectedPrimitiveIndices(),
      doc->isModified()};
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
    doc->restoreWorldSnapshot(data.world);
    doc->setSelectedPrimitiveIndices(data.selection);
    doc->setModified(data.docModified);
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
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