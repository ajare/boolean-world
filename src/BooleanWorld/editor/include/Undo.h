#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Document.h"
#include "Commands.h"

namespace editor {

struct HistoryItem {
  CommandId command;
  // Optional instance detail retained for diagnostics and old callers. UI
  // labels come from commandInfo(command), never from this string.
  std::string id;
  bool isUndo;
};

using UndoableActionFunction = std::function<bool(Document*)>;

bool canUndo();

bool canRedo();

size_t getUndoLevels();

size_t getRedoLevels();

void beginUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, float v, std::string detail = {});
void beginUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, wp::Vector2 const& v, std::string detail = {});

// Begins a gesture whose mutations are applied incrementally before commit.
void beginTransaction(Document* doc, CommandId command, float v, std::string detail = {});
void beginTransaction(Document* doc, CommandId command, wp::Vector2 const& v, std::string detail = {});

void commitUndoableAction(Document* doc);

void transact(Document* doc, CommandId command, std::function<void()> const& body, std::string detail = {});
void transactUndoableAction(Document* doc, CommandId command, UndoableActionFunction func, std::string detail = {});

// Compatibility entry points for integrations defining ad-hoc edits. Editor
// actions themselves use a typed CommandId. Their detail is not used as a UI
// label.
void beginUndoableAction(Document* doc, std::string const& detail, UndoableActionFunction func, float v);
void beginUndoableAction(Document* doc, std::string const& detail, UndoableActionFunction func, wp::Vector2 const& v);
void beginTransaction(Document* doc, std::string const& detail, float v);
void beginTransaction(Document* doc, std::string const& detail, wp::Vector2 const& v);
void commitUndoableAction(Document* doc, std::string const& detail);
void transact(Document* doc, std::string const& detail, std::function<void()> const& body);
void transactUndoableAction(Document* doc, std::string const& detail, UndoableActionFunction func);

// Executes immediately and records the snapshot only when the function
// succeeds. If the function returns false or throws, the world, selection,
// modification state, and history remain as they were.
bool transactUndoableActionAtomically(
    Document* doc,
    CommandId command,
    UndoableActionFunction func,
    std::string detail = {});

bool transactUndoableActionAtomically(
    Document* doc,
    std::string const& detail,
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