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

typedef std::function<bool(Document*)> UndoableActionFunction;

bool canUndo();

bool canRedo();

size_t getUndoLevels();

size_t getRedoLevels();

void beginUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func, float v);

void beginUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func, wp::Vector2 const& v);

void commitUndoableAction(Document* doc, std::string const& id = "");

void transactUndoableAction(Document* doc, std::string const& id, UndoableActionFunction func);

void abandonUndoableAction(Document* doc);

bool undoableActionInProgress();

bool transactionValueHasChanged(float v);

bool transactionValueHasChanged(wp::Vector2 const& v);

void undo(Document* doc, int count = 1);

void redo(Document* doc, int count = 1);

std::vector<HistoryItem> getActionHistory();

}  // namespace editor