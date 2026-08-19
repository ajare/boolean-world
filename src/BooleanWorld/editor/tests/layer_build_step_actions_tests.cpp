#include <iostream>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <core/LayerBuildStep.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

#include "Actions.h"
#include "Document.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();
editor::Settings gEditorSettings;

void openTiledPrefabFile(std::string const&, std::shared_ptr<bw::core::World>) {
}

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}
}  // namespace editor

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void disablingStepZeroRemovesItsPrimitivesAndRebuildRestoresThemOnReEnable() {
  editor::Document document;
  document.newDoc();
  auto world = document.getWorld();
  auto* layer = world->getActiveLayer();

  auto* authored = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd, 1.0f);
  authored->setPosition({5.0f, -3.0f});
  world->addPrimitive(authored);

  auto numPrimitivesBefore = layer->getNumPrimitives();
  require(numPrimitivesBefore >= 2, "fixture did not have at least the ghost and authored primitives");
  require(layer->getStep(0)->isEnabled(), "step 0 was not enabled by default");

  auto disableResult = editor::setLayerBuildStepEnabled(&document, layer, 0, false);
  require(disableResult, "disabling step 0 did not report success");
  require(!layer->getStep(0)->isEnabled(), "step 0 was not marked disabled");
  require(layer->getNumPrimitives() == 0, "disabling the only step did not remove all Primitives on rebuild");

  auto enableResult = editor::setLayerBuildStepEnabled(&document, layer, 0, true);
  require(enableResult, "re-enabling step 0 did not report success");
  require(layer->getStep(0)->isEnabled(), "step 0 was not marked enabled again");
  require(layer->getNumPrimitives() == numPrimitivesBefore,
          "re-enabling step 0 did not restore its Primitives exactly");
  require(layer->getPrimitive(numPrimitivesBefore - 1) == authored,
          "re-enabling step 0 did not restore the same authored Primitive");
}

void togglingStepEnabledIsOneUndoableActionThatRestoresLayerState() {
  editor::Document document;
  document.newDoc();
  auto world = document.getWorld();
  auto* layer = world->getActiveLayer();

  auto* authored = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd, 1.0f);
  authored->setPosition({12.0f, 4.0f});
  world->addPrimitive(authored);

  auto numPrimitivesBefore = layer->getNumPrimitives();
  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(&document, "Toggle Layer Step 0",
      std::bind(editor::setLayerBuildStepEnabled, std::placeholders::_1, layer, 0, false));

  require(editor::getUndoLevels() == undoBefore + 1,
          "toggling a step's enabled state did not create exactly one undo entry");
  require(document.getWorld()->getActiveLayer()->getNumPrimitives() == 0,
          "the toggle action did not remove the disabled step's Primitives");
  require(document.isModified(), "the toggle action did not mark the document modified");

  editor::undo(&document);
  require(document.getWorld()->getActiveLayer()->getStep(0)->isEnabled(),
          "undo did not restore step 0's enabled state");
  require(document.getWorld()->getActiveLayer()->getNumPrimitives() == numPrimitivesBefore,
          "undo did not restore the Layer's resulting Primitives");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  require(!document.getWorld()->getActiveLayer()->getStep(0)->isEnabled(),
          "redo did not restore step 0's disabled state");
  require(document.getWorld()->getActiveLayer()->getNumPrimitives() == 0,
          "redo did not restore the rebuilt (empty) Layer Primitives");
  require(document.isModified(), "redo did not restore the modified state");
}

}  // namespace

int main() {
  try {
    disablingStepZeroRemovesItsPrimitivesAndRebuildRestoresThemOnReEnable();
    togglingStepEnabledIsOneUndoableActionThatRestoresLayerState();
    std::cout << "Layer build step enable/disable action tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
