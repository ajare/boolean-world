#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/World.h>
#include <core/WorldDataGenerator.h>

#include "Document.h"
#include "Settings.h"
#include "Undo.h"
#include "UiHelpers.h"

spdlog::logger* gLogger = nullptr;
editor::Settings gEditorSettings;

void openTiledPrefabFile(std::string const&, std::shared_ptr<bw::core::World>) {
}

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}
}  // namespace editor

namespace {

class CopyCountingWorldDataGenerator final : public bw::core::WorldDataGenerator {
public:
  static inline int copyCount = 0;

  bw::core::WorldDataGenerator* copy() override {
    ++copyCount;
    return new CopyCountingWorldDataGenerator(*this);
  }

  bw::core::WorldDataPtr getWorldData(bw::core::World const*) override {
    return nullptr;
  }

  void generate(bw::core::World const*, bool) override {
  }
};

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void undoAndRedoPreserveEachIntermediateSnapshot() {
  editor::Document document;
  bw::core::World world(100.0f, 10.0f);
  world.setName("one");
  document.setWorld(world);
  document.setSelectedPrimitiveIndices({1});

  editor::transactUndoableAction(&document, "two", [](editor::Document* doc) {
    doc->getWorld()->setName("two");
    doc->setSelectedPrimitiveIndices({2});
    doc->setModified();
    return false;
  });
  editor::transactUndoableAction(&document, "three", [](editor::Document* doc) {
    doc->getWorld()->setName("three");
    doc->setSelectedPrimitiveIndices({3});
    return false;
  });
  editor::transactUndoableAction(&document, "four", [](editor::Document* doc) {
    doc->getWorld()->setName("four");
    doc->setSelectedPrimitiveIndices({4});
    return false;
  });

  editor::undo(&document, 2);
  require(document.getWorld()->getName() == "two" && document.getSelectedPrimitiveIndices() == std::set<uint32_t>({2}),
          "multi-step undo did not restore an intermediate snapshot");

  editor::redo(&document);
  require(document.getWorld()->getName() == "three" && document.getSelectedPrimitiveIndices() == std::set<uint32_t>({3}),
          "redo after multi-step undo did not restore the next snapshot");

  editor::undo(&document, 100);
  require(document.getWorld()->getName() == "one" && document.getSelectedPrimitiveIndices() == std::set<uint32_t>({1}) && !document.isModified(),
          "undo beyond the available history did not stop at the initial snapshot");

  editor::redo(&document, 100);
  require(document.getWorld()->getName() == "four" && document.getSelectedPrimitiveIndices() == std::set<uint32_t>({4}),
          "redo beyond the available history did not stop at the final snapshot");
}

void abandonedAndCommittedActionsClearTransactionValues() {
  editor::Document document;
  bw::core::World world(100.0f, 10.0f);
  document.setWorld(world);

  auto noChange = [](editor::Document*) { return false; };

  editor::beginUndoableAction(&document, "abandoned float", noChange, 1.0f);
  require(editor::undoableActionInProgress() && editor::transactionValueHasChanged(2.0f),
          "float transaction did not begin");
  editor::abandonUndoableAction(&document);
  require(!editor::undoableActionInProgress() && !editor::transactionValueHasChanged(2.0f),
          "abandoned float transaction retained its initial value");

  editor::beginUndoableAction(&document, "abandoned vector", noChange, wp::Vector2{1.0f, 1.0f});
  editor::abandonUndoableAction(&document);
  require(!editor::transactionValueHasChanged(wp::Vector2{2.0f, 1.0f}),
          "abandoned vector transaction retained its initial value");

  editor::beginUndoableAction(&document, "committed float", noChange, 3.0f);
  editor::commitUndoableAction(&document);
  require(!editor::transactionValueHasChanged(4.0f),
          "committed float transaction retained its initial value");

  editor::beginUndoableAction(&document, "committed vector", noChange, wp::Vector2{3.0f, 3.0f});
  editor::commitUndoableAction(&document);
  require(!editor::transactionValueHasChanged(wp::Vector2{4.0f, 3.0f}),
          "committed vector transaction retained its initial value");
}

void historyDoesNotCopyUndoOrRedoWorldSnapshots() {
  CopyCountingWorldDataGenerator::copyCount = 0;
  editor::Document document;
  bw::core::World world(
      100.0f,
      10.0f,
      [](wp::Vector2, int, int, float) {
        return new CopyCountingWorldDataGenerator;
      });
  document.setWorld(world);

  editor::transactUndoableAction(&document, "first", [](editor::Document*) {
    return false;
  });
  editor::transactUndoableAction(&document, "second", [](editor::Document*) {
    return false;
  });
  editor::undo(&document);

  auto const copiesBeforeHistory = CopyCountingWorldDataGenerator::copyCount;
  auto const history = editor::getActionHistory();

  require(history.size() >= 2, "history did not retain the undo and redo entries");
  require(history[history.size() - 2].id == "first" && history[history.size() - 2].isUndo,
          "history did not retain the undo entry");
  require(history.back().id == "second" && !history.back().isUndo,
          "history did not retain the redo entry");
  require(CopyCountingWorldDataGenerator::copyCount == copiesBeforeHistory,
          "reading history copied an undo or redo world snapshot");
}

}  // namespace

int main() {
  try {
    undoAndRedoPreserveEachIntermediateSnapshot();
    abandonedAndCommittedActionsClearTransactionValues();
    historyDoesNotCopyUndoOrRedoWorldSnapshots();
    std::cout << "Undo history regressions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
