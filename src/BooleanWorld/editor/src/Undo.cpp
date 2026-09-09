#include <deque>
#include <set>
#include <ranges>

#include <common/BoundedDeque.h>

#include <core/DefinePrefabs.h>
#include <core/PrefabField.h>
#include <core/TileMap.h>
#include <core/World.h>

#include "Undo.h"
#include "Document.h"
#include "EditorException.h"
#include "EmbossingCatalogLibrary.h"
#include "ProcMaterialLibrary.h"
#include "UiHelpers.h"
#include "Settings.h"

#define MAX_STACK_SIZE 20

extern editor::Settings gEditorSettings;

namespace editor {
using namespace std;

struct PrefabFocus {
  uint32_t layerId{~0u};
  uint32_t stepIndex{~0u};
  uint32_t prefabId{~0u};
};

struct PrefabFieldFocus {
  uint32_t layerId{~0u};
  uint32_t stepIndex{~0u};
  uint32_t prefabId{~0u};
  bw::core::Tile tile{};
  bool hasTile{false};
};

struct TileMapFocus {
  uint32_t layerId{~0u};
  uint32_t stepIndex{~0u};
};

struct UndoData {
  WorldSnapshot world;
  set<uint32_t> selection;
  uint32_t selectedWorldVertex{~0u};
  uint32_t selectedTriggerLine{~0u};
  uint32_t activeMeshPrimitive{~0u};
  set<uint32_t> selectedMeshVertices;
  set<uint32_t> selectedMeshEdges;
  set<uint32_t> selectedMeshRings;
  vector<PrefabFocus> prefabFocus;
  vector<PrefabFieldFocus> prefabFieldFocus;
  optional<TileMapFocus> tileMapFocus;
  ProcMaterialLibrarySnapshot procMaterials;
  EmbossingCatalogSnapshot embossingCatalog;
  bool docModified{false};
};

struct UndoEntry {
  CommandId command{CommandId::Edit};
  std::string detail;
  UndoData data;
};

// Internal
static std::deque<UndoEntry> gUndoStack, gRedoStack;
static UndoData gTransactionalData;
static CommandId gTransactionalCommand{CommandId::Edit};
static std::string gTransactionalDetail;
static float gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
static wp::Vector2 gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};
static UndoableActionFunction gTransactionalFunc;

UndoData captureUndoData(Document* doc) {
  vector<PrefabFocus> prefabFocus;
  vector<PrefabFieldFocus> prefabFieldFocus;
  optional<TileMapFocus> tileMapFocus;
  if (doc->isActive()) {
    for (auto const* layer : doc->getWorld()->getLayers()) {
      if (dynamic_cast<bw::core::TileMap const*>(layer->getActiveStep())) {
        tileMapFocus = TileMapFocus{layer->getId(), layer->getActiveStepIndex()};
      }
      for (uint32_t stepIndex = 0; stepIndex < layer->getNumSteps(); ++stepIndex) {
        auto const* step = dynamic_cast<bw::core::DefinePrefabs const*>(
            layer->getStep(stepIndex));
        if (step && step->getSelectedPrefab()) {
          prefabFocus.push_back(
              {layer->getId(), stepIndex, step->getSelectedPrefab()->getId()});
        }
        auto const* field = dynamic_cast<bw::core::PrefabField const*>(
            layer->getStep(stepIndex));
        if (field) {
          auto const* selected = field->getSelectedPrefab(*layer);
          prefabFieldFocus.push_back(
              {layer->getId(), stepIndex, selected ? selected->getId() : ~0u,
               field->hasSelectedTile() ? field->getSelectedTile() : bw::core::Tile{},
               field->hasSelectedTile()});
        }
      }
    }
  }

  return {
      doc->captureWorldSnapshot(),
      doc->getSelectedPrimitiveIndices(),
      doc->getSelectedWorldVertexIndex(),
      doc->getSelectedTriggerLineIndex(),
      doc->getActiveMeshPrimitiveIndex(),
      doc->getSelectedMeshVertexIndices(),
      doc->getSelectedMeshEdgeIndices(),
      doc->getSelectedMeshRingIndices(),
      move(prefabFocus),
      move(prefabFieldFocus),
      tileMapFocus,
      procMaterialLibrary().captureSnapshot(),
      embossingCatalogLibrary().captureSnapshot(),
      doc->isModified()};
}

void restoreUndoData(Document* doc, UndoData const& data) {
  doc->restoreWorldSnapshot(data.world);
  procMaterialLibrary().restoreSnapshot(data.procMaterials);
  embossingCatalogLibrary().restoreSnapshot(data.embossingCatalog);
  for (auto const& focus : data.prefabFocus) {
    auto* layer = doc->getWorld()->getLayer(focus.layerId);
    if (!layer || focus.stepIndex >= layer->getNumSteps()) {
      continue;
    }
    auto* step = dynamic_cast<bw::core::DefinePrefabs*>(
        layer->getStep(focus.stepIndex));
    if (step) {
      step->setSelectedPrefab(step->findPrefabById(focus.prefabId));
      layer->rebuild();
    }
  }
  if (data.tileMapFocus) {
    auto* layer = doc->getWorld()->getLayer(data.tileMapFocus->layerId);
    if (layer && data.tileMapFocus->stepIndex < layer->getNumSteps() &&
        dynamic_cast<bw::core::TileMap*>(
            layer->getStep(data.tileMapFocus->stepIndex))) {
      doc->getWorld()->setActiveLayer(layer);
      layer->setActiveStep(data.tileMapFocus->stepIndex);
    }
  }
  for (auto const& focus : data.prefabFieldFocus) {
    auto* layer = doc->getWorld()->getLayer(focus.layerId);
    if (!layer || focus.stepIndex >= layer->getNumSteps()) continue;
    auto* field = dynamic_cast<bw::core::PrefabField*>(layer->getStep(focus.stepIndex));
    if (!field) continue;
    auto* definitions = field->getDefinePrefabs(*layer);
    if (definitions && focus.prefabId != ~0u) {
      field->setSelectedPrefab(*definitions, definitions->findPrefabById(focus.prefabId));
    }
    if (focus.hasTile) field->selectTile(focus.tile);
  }
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

void beginUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, float v, string detail) {
  if (gTransactionalFunc) commitUndoableAction(doc);
  gTransactionalCommand = command;
  gTransactionalDetail = move(detail);
  gTransactionalData = captureUndoData(doc);
  gTransactionalInitialFloatValue = v;
  gTransactionalFunc = move(func);
}

void beginUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, wp::Vector2 const& v, string detail) {
  if (gTransactionalFunc) commitUndoableAction(doc);
  gTransactionalCommand = command;
  gTransactionalDetail = move(detail);
  gTransactionalData = captureUndoData(doc);
  gTransactionalInitialVectorValue = v;
  gTransactionalFunc = move(func);
}

void beginTransaction(Document* doc, CommandId command, float v, string detail) {
  beginUndoableAction(doc, command, [](Document*) { return true; }, v, move(detail));
}

void beginTransaction(Document* doc, CommandId command, wp::Vector2 const& v, string detail) {
  beginUndoableAction(doc, command, [](Document*) { return true; }, v, move(detail));
}

void beginUndoableAction(Document* doc, string const& detail, UndoableActionFunction func, float v) {
  beginUndoableAction(doc, CommandId::Edit, move(func), v, detail);
}
void beginUndoableAction(Document* doc, string const& detail, UndoableActionFunction func, wp::Vector2 const& v) {
  beginUndoableAction(doc, CommandId::Edit, move(func), v, detail);
}
void beginTransaction(Document* doc, string const& detail, float v) {
  beginTransaction(doc, CommandId::Edit, v, detail);
}
void beginTransaction(Document* doc, string const& detail, wp::Vector2 const& v) {
  beginTransaction(doc, CommandId::Edit, v, detail);
}

bool transactionValueHasChanged(float v) {
  return !isnan(gTransactionalInitialFloatValue) && gTransactionalInitialFloatValue != v;
}

bool transactionValueHasChanged(wp::Vector2 const& v) {
  return !isnan(gTransactionalInitialVectorValue.x) && gTransactionalInitialVectorValue != v;
}

void commitUndoableAction(Document* doc) {
  gRedoStack.clear();

  UndoEntry data{gTransactionalCommand, gTransactionalDetail, gTransactionalData};
  gUndoStack.push_back(data);
  bw::common::trimDequeToCapacity(gUndoStack, MAX_STACK_SIZE);

  auto func = gTransactionalFunc;
  auto clearTransactionalState = [] {
    gTransactionalFunc = nullptr;
    gTransactionalCommand = CommandId::Edit;
    gTransactionalDetail.clear();
    gTransactionalData = {};
    gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
    gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};
  };

  bool modified;
  try {
    modified = func(doc);
  } catch (...) {
    // The entry pushed above belongs to this action alone - nothing else can
    // have touched gUndoStack between the push and here, so it is safe to
    // pop unconditionally and leave no transaction in progress.
    gUndoStack.pop_back();
    clearTransactionalState();
    throw;
  }

  if (modified) {
    doc->setModified();
  }

  clearTransactionalState();

  // Indices in the Document's selection may have been invalidated by the
  // action just committed - e.g. a delete rebuilds the Layer's Primitive
  // list and re-stamps every id from its new position (ticket #198).
  doc->revalidateSelection();

  // The arrangement and everything drawn from it are derived from the World,
  // so an action that changed the World has just made them stale. This is the
  // one place that needs to say so: every action commits through here.
  //
  // Regeneration is asynchronous, and a request that has not started is
  // replaced by the next one, while one already running has its result
  // discarded - so a run of quick edits collapses onto the last of them.
  regenerateWorldData(doc);
}

void commitUndoableAction(Document* doc, string const& detail) {
  gTransactionalDetail = detail;
  commitUndoableAction(doc);
}

void transact(Document* doc, CommandId command, function<void()> const& body, string detail) {
  transactUndoableAction(doc, command, [&](Document*) {
    body();
    return true;
  }, move(detail));
}

void transactUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, string detail) {
  beginUndoableAction(doc, command, move(func), numeric_limits<float>::quiet_NaN(), move(detail));
  commitUndoableAction(doc);
}

void transact(Document* doc, string const& detail, function<void()> const& body) {
  transact(doc, CommandId::Edit, body, detail);
}
void transactUndoableAction(Document* doc, string const& detail, UndoableActionFunction func) {
  transactUndoableAction(doc, CommandId::Edit, move(func), detail);
}

bool transactUndoableActionAtomically(
    Document* doc, CommandId command, UndoableActionFunction func,
    string detail) {
  if (gTransactionalFunc) {
    throw EditorException(
        "Cannot run an atomic action while another undoable action is in progress.");
  }

  auto previous = captureUndoData(doc);
  UndoEntry entry{command, move(detail), previous};
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
    doc->revalidateSelection();
    regenerateWorldData(doc);
    return true;
  } catch (...) {
    restorePrevious();
    throw;
  }
}

bool transactUndoableActionAtomically(
    Document* doc, string const& detail, UndoableActionFunction func) {
  return transactUndoableActionAtomically(doc, CommandId::Edit, move(func), detail);
}

void abandonUndoableAction(Document* doc) {
  gTransactionalCommand = CommandId::Edit;
  gTransactionalDetail.clear();

  gTransactionalData = {};
  gTransactionalData.docModified = doc->isModified();
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};

  gTransactionalFunc = nullptr;
}

void cancelUndoableAction(Document* doc) {
  if (!gTransactionalFunc) {
    return;
  }

  // Restore from a copy: restoreUndoData rebuilds the World, and nothing
  // about this action should still be in progress while that runs.
  auto data = move(gTransactionalData);
  gTransactionalFunc = nullptr;
  gTransactionalCommand = CommandId::Edit;
  gTransactionalDetail.clear();
  gTransactionalData = {};
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};

  restoreUndoData(doc, data);
  doc->revalidateSelection();
  regenerateWorldData(doc);
}

bool undoableActionInProgress() {
  return static_cast<bool>(gTransactionalFunc);
}

void clearUndoHistory() {
  gUndoStack.clear();
  gRedoStack.clear();

  gTransactionalFunc = nullptr;
  gTransactionalCommand = CommandId::Edit;
  gTransactionalDetail.clear();
  gTransactionalData = {};
  gTransactionalInitialFloatValue = numeric_limits<float>::quiet_NaN();
  gTransactionalInitialVectorValue = {numeric_limits<float>::quiet_NaN(), numeric_limits<float>::quiet_NaN()};
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
    auto command = oldEntry.command;
    auto detail = move(oldEntry.detail);

    gRedoStack.push_back({command, move(detail), move(data)});
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
    auto command = oldEntry.command;
    auto detail = move(oldEntry.detail);

    gUndoStack.push_back({command, move(detail), move(data)});
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
    entries.push_back({item.command, item.detail, true});
  }

  ranges::reverse_view redoStackRev{gRedoStack};
  for (auto const& item : redoStackRev) {
    entries.push_back({item.command, item.detail, false});
  }

  return entries;
}

}  // namespace editor