#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Document.h"

namespace editor {

struct HistoryItem {
  std::string id;
  bool isUndo;
};

using UndoableActionFunction = std::function<bool(Document*)>;

bool canUndo();

bool canRedo();

size_t getUndoLevels();

size_t getRedoLevels();

void beginUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func, float v);

void beginUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func, wp::Vector2 const& v);

// Begins a gesture whose mutations are applied incrementally before commit.
void beginTransaction(Document* doc, std::string const& id, float v);

void beginTransaction(Document* doc, std::string const& id, wp::Vector2 const& v);

void commitUndoableAction(Document* doc, std::string const& id = "");

void transact(Document* doc, std::string const& name, std::function<void()> const& body);

void transactUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func);

// Executes immediately and records the snapshot only when the function
// succeeds. If the function returns false or throws, the world, selection,
// modification state, and history remain as they were.
bool transactUndoableActionAtomically(
    Document* doc,
    std::string const& id,
    UndoableActionFunction func);

void abandonUndoableAction(Document* doc);

// Discards an action begun with beginUndoableAction, restoring the World,
// selection and modified flag captured when it began. Nothing reaches the
// undo stack, so the whole gesture leaves no trace in the history. Does
// nothing when no action is in progress.
void cancelUndoableAction(Document* doc);

bool undoableActionInProgress();

void clearUndoHistory();

bool transactionValueHasChanged(float v);

bool transactionValueHasChanged(wp::Vector2 const& v);

void undo(Document* doc, int count = 1);

void redo(Document* doc, int count = 1);

std::vector<HistoryItem> getActionHistory();

}  // namespace editor