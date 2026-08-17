#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/DynamicWorldDataGenerator.h>
#include <core/RectanglePolygon.h>
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
  document.newDoc();
  document.getWorld()->setName("one");
  document.setSelectedPrimitiveIndices({1});

  editor::transactUndoableAction(&document, "two", [](editor::Document* doc) {
    doc->getWorld()->setName("two");
    doc->getWorld()->getPrimitive(0)->setOperation(
        bw::core::Primitive::Operation::Difference);
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
  require(document.getWorld()->getPrimitive(0)->getOperation() == bw::core::Primitive::Operation::Union,
          "undo did not restore the editor ghost from the runtime-free snapshot");

  editor::redo(&document, 100);
  require(document.getWorld()->getName() == "four" && document.getSelectedPrimitiveIndices() == std::set<uint32_t>({4}),
          "redo beyond the available history did not stop at the final snapshot");
  require(document.getWorld()->getPrimitive(0)->getOperation() == bw::core::Primitive::Operation::Difference,
          "redo did not restore the editor ghost from the runtime-free snapshot");
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

void runtimeFreeSnapshotPreservesEditorGenerationConfiguration() {
  editor::Document document;
  document.newDoc();
  auto world = document.getWorld();
  world->setName("snapshot");
  world->setAlwaysUpdateVertices(true);
  auto generator = static_cast<bw::core::DynamicWorldDataGenerator*>(
      world->getWorldDataGenerator());
  generator->setAlwaysUpdateVertices(false);
  generator->setAllowCommitIfVisible(false);
  generator->setActiveLayer(9);
  generator->setScheduledGenerationInterval(3.5f);

  auto snapshot = document.captureWorldSnapshot();
  require(world->isModified(),
          "capturing an undo snapshot cleared the live world's modification state");
  document.restoreWorldSnapshot(snapshot);

  auto restoredWorld = document.getWorld();
  auto restoredGenerator = static_cast<bw::core::DynamicWorldDataGenerator*>(
      restoredWorld->getWorldDataGenerator());
  require(restoredWorld->getName() == "snapshot" &&
              restoredWorld->getAlwaysUpdateVertices(),
          "runtime-free snapshot lost world authoring state");
  require(!restoredGenerator->getAlwaysUpdateVertices() &&
              !restoredGenerator->getAllowCommitIfVisible() &&
              restoredGenerator->getLayerSelection() == bw::core::SelectLayer(9) &&
              restoredGenerator->getScheduledGenerationInterval() == 3.5f,
          "runtime-free snapshot lost editor generation configuration");
}

void copiedDynamicGeneratorRetainsItsWorldAndSettings() {
  bw::core::World world(100.0f, 10.0f);
  world.addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));

  bw::core::DynamicWorldDataGenerator generator(&world);
  generator.setAlwaysUpdateVertices(true);
  generator.setAllowCommitIfVisible(true);
  generator.setActiveLayer(7);
  generator.setScheduledGenerationInterval(2.5f);

  auto copyBase = std::unique_ptr<bw::core::WorldDataGenerator>(generator.copy());
  auto copy = static_cast<bw::core::DynamicWorldDataGenerator*>(copyBase.get());
  require(copy->getAlwaysUpdateVertices() && copy->getAllowCommitIfVisible(),
          "dynamic generator copy lost its generation settings");
  require(copy->getLayerSelection() == generator.getLayerSelection() &&
              copy->getScheduledGenerationInterval() == 2.5f,
          "dynamic generator copy lost its layer or schedule settings");

  copy->generateBlocking();
  require(copy->getNumGenerationsComplete() == 1,
          "dynamic generator copy could not generate from its retained world");
}

void historyDoesNotCopyUndoOrRedoWorldSnapshots() {
  CopyCountingWorldDataGenerator::copyCount = 0;
  editor::Document document;
  bw::core::World world(100.0f, 10.0f);
  world.setWorldDataGenerator(new CopyCountingWorldDataGenerator);
  world.addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f));
  document.setWorld(world);
  CopyCountingWorldDataGenerator::copyCount = 0;

  editor::transactUndoableAction(&document, "first", [](editor::Document*) {
    return false;
  });
  editor::transactUndoableAction(&document, "second", [](editor::Document*) {
    return false;
  });
  editor::undo(&document);

  require(CopyCountingWorldDataGenerator::copyCount == 0,
          "recording or restoring undo history copied a live world data generator");
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
    runtimeFreeSnapshotPreservesEditorGenerationConfiguration();
    copiedDynamicGeneratorRetainsItsWorldAndSettings();
    historyDoesNotCopyUndoOrRedoWorldSnapshots();
    std::cout << "Undo history regressions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
