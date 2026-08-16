#include <deque>
#include <set>
#include <ranges>

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
  bw::core::World world;
  set<uint32_t> selection;
  bool docModified;
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

  gTransactionalData.world = *doc->getWorld();
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

  gTransactionalData.world = *doc->getWorld();
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
  while (gUndoStack.size() >= MAX_STACK_SIZE) {
    gUndoStack.pop_front();
  }

  gRedoStack.clear();

  UndoEntry data{id != "" ? id : gTransactionalId, gTransactionalData};
  gUndoStack.push_back(data);

  if (gTransactionalFunc(doc)) {
    doc->setModified();
  }

  gTransactionalFunc = nullptr;
  gTransactionalId.clear();
  gTransactionalData.world.clear();
  gTransactionalData.selection.clear();
}

void transactUndoableAction(Document* doc, string const& id, UndoableActionFunction func) {
  beginUndoableAction(doc, id, func, numeric_limits<float>::quiet_NaN());
  commitUndoableAction(doc);
}

void abandonUndoableAction(Document* doc) {
  gTransactionalId.clear();

  gTransactionalData.world.clear();
  gTransactionalData.selection.clear();
  gTransactionalData.docModified = doc->isModified();

  gTransactionalFunc = nullptr;
}

bool undoableActionInProgress() {
  return !gTransactionalId.empty();
}

void undo(Document* doc, int count) {
  auto world = *doc->getWorld();
  auto const& selection = doc->getSelectedPrimitiveIndices();
  auto modified = doc->isModified();

  for (int i = 0; i < count; ++i) {
    auto const& oldEntry = gUndoStack.back();

    gRedoStack.push_back({oldEntry.id, {world, selection, modified}});

    if (i == (count - 1)) {
      doc->setWorld(oldEntry.data.world);
      doc->setSelectedPrimitiveIndices(oldEntry.data.selection);
      doc->setModified(oldEntry.data.docModified);
    }

    gUndoStack.pop_back();
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_UNDO_REDO);
}

void redo(Document* doc, int count) {
  auto curWorld = *doc->getWorld();
  auto const& curSelection = doc->getSelectedPrimitiveIndices();
  auto modified = doc->isModified();

  for (int i = 0; i < count; ++i) {
    auto const& oldEntry = gRedoStack.back();

    gUndoStack.push_back({oldEntry.id, {curWorld, curSelection, modified}});

    if (i == (count - 1)) {
      doc->setWorld(oldEntry.data.world);
      doc->setSelectedPrimitiveIndices(oldEntry.data.selection);
      doc->setModified(oldEntry.data.docModified);
    }

    gRedoStack.pop_back();
  }

  generateClipping(doc, gEditorSettings, ED_CLIP_ON_PRIM_SETTING_CHANGE);
}

vector<HistoryItem> getActionHistory() {
  vector<HistoryItem> entries;

  for (auto item : gUndoStack) {
    entries.push_back({item.id, true});
  }

  ranges::reverse_view redoStackRev{gRedoStack};
  for (auto item : redoStackRev) {
    entries.push_back({item.id, false});
  }

  return entries;
}

}  // namespace editor