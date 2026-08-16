#include <iostream>
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

  require(history.size() == 2, "history did not retain the undo and redo entries");
  require(history[0].id == "first" && history[0].isUndo,
          "history did not retain the undo entry");
  require(history[1].id == "second" && !history[1].isUndo,
          "history did not retain the redo entry");
  require(CopyCountingWorldDataGenerator::copyCount == copiesBeforeHistory,
          "reading history copied an undo or redo world snapshot");
}

}  // namespace

int main() {
  try {
    historyDoesNotCopyUndoOrRedoWorldSnapshots();
    std::cout << "Undo history avoids world snapshot copies\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
