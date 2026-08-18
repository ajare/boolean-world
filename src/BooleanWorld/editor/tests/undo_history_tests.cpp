#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/DynamicWorldDataGenerator.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
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

void undoAndRedoRestoreNonPrimitiveSelections() {
  editor::Document document;
  document.newDoc();
  document.setSelectedTriggerLineIndex(7);

  editor::transactUndoableAction(&document, "select primitive", [](editor::Document* doc) {
    doc->setSelectedPrimitiveIndices({0});
    return false;
  });
  editor::undo(&document);
  require(document.getSelectedPrimitiveIndices().empty() &&
              document.getSelectedTriggerLineIndex() == 7,
          "undo did not restore a trigger-line selection");

  editor::redo(&document);
  require(document.getSelectedPrimitiveIndices() == std::set<uint32_t>{0} &&
              document.getSelectedTriggerLineIndex() == ~0u,
          "redo did not restore the corresponding primitive selection");
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

void superformulaControlValueEditIsDirtyAndUndoable() {
  float values[6] = {1.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  bw::core::World world(100.0f, 10.0f);
  world.addPrimitive(new bw::core::SuperformulaPolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      0.5f,
      values));

  editor::Document document;
  document.setWorld(world);
  document.setModified(false);

  auto getSuperformula = [&document]() {
    return static_cast<bw::core::SuperformulaPolygon*>(document.getWorld()->getPrimitive(0));
  };
  auto const originalValue = getSuperformula()->getValue(0);
  auto const originalRadius = getSuperformula()->getRadius();
  auto const undoLevelsBeforeEdit = editor::getUndoLevels();

  editor::beginUndoableAction(&document, "", [](editor::Document*) { return true; }, originalValue);
  getSuperformula()->setValue(0, 1.25f);
  getSuperformula()->setValue(0, 1.75f);
  editor::commitUndoableAction(&document, "Set Superformula a to 1.75");

  auto const editedRadius = getSuperformula()->getRadius();
  require(document.isModified(),
          "editing a superformula control value did not mark the document modified");
  require(editor::getUndoLevels() == undoLevelsBeforeEdit + 1,
          "one superformula control drag did not create exactly one history entry");
  require(getSuperformula()->getValue(0) == 1.75f && editedRadius != originalRadius,
          "editing a superformula control value did not regenerate its contour");

  editor::undo(&document);
  require(!document.isModified() && getSuperformula()->getValue(0) == originalValue &&
              getSuperformula()->getRadius() == originalRadius,
          "undo did not restore the superformula control value and contour");

  editor::redo(&document);
  require(document.isModified() && getSuperformula()->getValue(0) == 1.75f &&
              getSuperformula()->getRadius() == editedRadius,
          "redo did not restore the superformula control value and contour");
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

void undoHistoryRetainsConfiguredCapacity() {
  editor::Document document;
  document.newDoc();

  for (int i = 0; i <= 20; ++i) {
    editor::transactUndoableAction(&document, "capacity " + std::to_string(i), [](editor::Document*) {
      return false;
    });
  }

  auto const history = editor::getActionHistory();
  require(editor::getUndoLevels() == 20 && history.size() >= 20 &&
              history[history.size() - 20].id == "capacity 1" &&
              history.back().id == "capacity 20",
          "undo history did not retain its configured capacity");
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
    undoAndRedoRestoreNonPrimitiveSelections();
    abandonedAndCommittedActionsClearTransactionValues();
    superformulaControlValueEditIsDirtyAndUndoable();
    runtimeFreeSnapshotPreservesEditorGenerationConfiguration();
    copiedDynamicGeneratorRetainsItsWorldAndSettings();
    undoHistoryRetainsConfiguredCapacity();
    historyDoesNotCopyUndoOrRedoWorldSnapshots();
    std::cout << "Undo history regressions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
