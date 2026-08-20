#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <core/CoreException.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

#include "Actions.h"
#include "Document.h"
#include "Settings.h"
#include "Undo.h"

spdlog::logger* gLogger = spdlog::default_logger_raw();
editor::Settings gEditorSettings;

namespace editor {
void generateClipping(Document*, Settings const&, int) {
}

void regenerateWorldData(Document*) {
}
}  // namespace editor

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireCoreException(Function&& function, std::string const& message) {
  try {
    function();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

std::vector<std::string> stepTypes(bw::core::Layer const& layer) {
  std::vector<std::string> types;
  for (uint32_t i = 0; i < layer.getNumSteps(); ++i) {
    types.push_back(layer.getStep(i)->getType());
  }
  return types;
}

bw::core::RectanglePolygon* makeRectangle(float x) {
  auto* rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd, 1.0f);
  rect->setPosition({x, 0.0f});
  return rect;
}

// A step is only ever handed to a Layer, which owns it from then on.
bw::core::PrimitiveField* makeField(std::vector<float> const& positions) {
  auto* field = new bw::core::PrimitiveField();
  for (auto x : positions) {
    field->addPrimitive(makeRectangle(x));
  }
  return field;
}

std::vector<float> builtPositions(bw::core::Layer const& layer) {
  std::vector<float> positions;
  for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) {
    positions.push_back(layer.getPrimitive(i)->getPosition().x);
  }
  return positions;
}

// A newDoc() Layer starts with an editor-only ghost Primitive, so the build
// order is checked via the relative order of two marker positions rather
// than the full list.
bool comesBefore(bw::core::Layer const& layer, float first, float second) {
  auto positions = builtPositions(layer);
  auto firstIt = std::find(positions.begin(), positions.end(), first);
  auto secondIt = std::find(positions.begin(), positions.end(), second);
  return firstIt != positions.end() && secondIt != positions.end() && firstIt < secondIt;
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

void addingAStepAppendsAPrimitiveFieldAsOneUndoableAction() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(&document, "Add Layer Step",
      std::bind(editor::addLayerBuildStep, std::placeholders::_1, layer));

  require(editor::getUndoLevels() == undoBefore + 1,
          "adding a step did not create exactly one undo entry");
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 2 && layer->getStep(1)->getType() == "PrimitiveField",
          "adding a step did not append a new PrimitiveField step at the end");
  require(document.isModified(), "adding a step did not mark the document modified");

  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 1, "undo did not remove the added step");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 2 && layer->getStep(1)->getType() == "PrimitiveField",
          "redo did not restore the added step");
  require(document.isModified(), "redo did not restore the modified state");
}

void removingANonFirstStepIsOneUndoableActionAndTheFirstStepIsRejected() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  layer->addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::EvenOdd, 1.0f));
  layer->addStep(new bw::core::PrimitiveField());
  layer->addStep(new bw::core::PrimitiveField());

  auto numPrimitivesBefore = layer->getNumPrimitives();
  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(&document, "Remove Layer Step",
      std::bind(editor::removeLayerBuildStep, std::placeholders::_1, layer, 1));

  require(editor::getUndoLevels() == undoBefore + 1,
          "removing a step did not create exactly one undo entry");
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 2, "removing a step did not shorten the Layer's step list");
  require(document.isModified(), "removing a step did not mark the document modified");

  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 3, "undo did not restore the removed step");
  require(layer->getNumPrimitives() == numPrimitivesBefore,
          "undo did not restore the Layer's resulting Primitives");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 2, "redo did not restore the step removal");
  require(document.isModified(), "redo did not restore the modified state");

  requireCoreException(
      [&] { editor::removeLayerBuildStep(&document, layer, 0); },
      "removing a Layer's first step was not rejected");
  require(layer->getNumSteps() == 2 && layer->getStep(0)->getType() == "PrimitiveField",
          "a rejected removal of the first step disturbed the Layer's step list");
}

void movingAStepIsOneUndoableActionAndMovesIntoOrOutOfIndexZeroAreRejected() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  layer->addPrimitive(makeRectangle(0.0f));
  layer->addStep(makeField({10.0f}));
  layer->addStep(makeField({20.0f}));

  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(&document, "Move Layer Step",
      std::bind(editor::moveLayerBuildStep, std::placeholders::_1, layer, 1, 2));

  require(editor::getUndoLevels() == undoBefore + 1,
          "moving a step did not create exactly one undo entry");
  layer = document.getWorld()->getActiveLayer();
  require(comesBefore(*layer, 20.0f, 10.0f),
          "moving a step did not change the build order to match its new position");
  require(document.isModified(), "moving a step did not mark the document modified");

  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(comesBefore(*layer, 10.0f, 20.0f),
          "undo did not restore the Layer's original build order");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(comesBefore(*layer, 20.0f, 10.0f),
          "redo did not restore the reordered build order");
  require(document.isModified(), "redo did not restore the modified state");

  auto typesBefore = stepTypes(*layer);
  requireCoreException(
      [&] { editor::moveLayerBuildStep(&document, layer, 0, 1); },
      "moving the first step out of index 0 was not rejected");
  requireCoreException(
      [&] { editor::moveLayerBuildStep(&document, layer, 1, 0); },
      "moving another step into index 0 was not rejected");
  require(stepTypes(*layer) == typesBefore,
          "a rejected move disturbed the Layer's step list");
}

void movingAPrimitiveToAnotherLayerLandsInItsFirstStepAsOneUndoableAction() {
  editor::Document document;
  document.newDoc();
  auto world = document.getWorld();
  auto* sourceLayer = world->getActiveLayer();
  auto sourceLayerId = sourceLayer->getId();
  auto* destinationLayer = world->addLayer("Destination");
  auto destinationLayerId = destinationLayer->getId();

  auto* moved = makeRectangle(7.0f);
  world->addPrimitive(moved);

  auto sourceCountBefore = sourceLayer->getNumPrimitives();
  auto destCountBefore = destinationLayer->getNumPrimitives();
  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(&document, "Move Primitive to Layer",
      std::bind(editor::movePrimitiveToLayer, std::placeholders::_1, moved, destinationLayer));

  require(editor::getUndoLevels() == undoBefore + 1,
          "moving a Primitive to another Layer did not create exactly one undo entry");
  world = document.getWorld();
  sourceLayer = world->getLayer(sourceLayerId);
  destinationLayer = world->getLayer(destinationLayerId);
  require(sourceLayer->getNumPrimitives() == sourceCountBefore - 1,
          "moving a Primitive did not remove it from the source Layer");
  require(destinationLayer->getNumPrimitives() == destCountBefore + 1,
          "moving a Primitive did not add it to the destination Layer");
  require(destinationLayer->getPrimitiveField()->getNumPrimitives() == 1,
          "the moved Primitive did not land in the destination Layer's first step");
  require(document.isModified(), "moving a Primitive to another Layer did not mark the document modified");

  editor::undo(&document);
  world = document.getWorld();
  sourceLayer = world->getLayer(sourceLayerId);
  destinationLayer = world->getLayer(destinationLayerId);
  require(sourceLayer->getNumPrimitives() == sourceCountBefore,
          "undo did not restore the source Layer's resulting Primitives");
  require(destinationLayer->getNumPrimitives() == destCountBefore,
          "undo did not restore the destination Layer's resulting Primitives");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  world = document.getWorld();
  sourceLayer = world->getLayer(sourceLayerId);
  destinationLayer = world->getLayer(destinationLayerId);
  require(sourceLayer->getNumPrimitives() == sourceCountBefore - 1,
          "redo did not restore the source Layer's resulting Primitives");
  require(destinationLayer->getNumPrimitives() == destCountBefore + 1,
          "redo did not restore the destination Layer's resulting Primitives");
  require(destinationLayer->getPrimitiveField()->getNumPrimitives() == 1,
          "redo did not restore the moved Primitive in the destination Layer's first step");
  require(document.isModified(), "redo did not restore the modified state");
}

void selectingTheActiveStepRedirectsCreatedPrimitivesAndIsNotUndoable() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto secondIndex = layer->addStep(new bw::core::PrimitiveField());

  auto firstStepCountBefore = layer->getPrimitiveField()->getNumPrimitives();
  auto undoBefore = editor::getUndoLevels();
  document.setModified(false);

  layer->setActiveStep(secondIndex);

  require(editor::getUndoLevels() == undoBefore,
          "selecting the active step created an undo entry, but it is ephemeral editor focus");
  require(!document.isModified(),
          "selecting the active step marked the document modified, but it is ephemeral editor focus");

  auto* authored = makeRectangle(3.0f);
  document.getWorld()->addPrimitive(authored);

  require(layer->getStep(secondIndex)->getType() == "PrimitiveField" &&
              static_cast<bw::core::PrimitiveField*>(layer->getStep(secondIndex))->getPrimitive(0) == authored,
          "a Primitive created while the second step was active did not land there");
  require(layer->getPrimitiveField()->getNumPrimitives() == firstStepCountBefore,
          "a Primitive created while the second step was active reached the first step instead");
}

}  // namespace

int main() {
  try {
    disablingStepZeroRemovesItsPrimitivesAndRebuildRestoresThemOnReEnable();
    togglingStepEnabledIsOneUndoableActionThatRestoresLayerState();
    addingAStepAppendsAPrimitiveFieldAsOneUndoableAction();
    removingANonFirstStepIsOneUndoableActionAndTheFirstStepIsRejected();
    movingAStepIsOneUndoableActionAndMovesIntoOrOutOfIndexZeroAreRejected();
    movingAPrimitiveToAnotherLayerLandsInItsFirstStepAsOneUndoableAction();
    selectingTheActiveStepRedirectsCreatedPrimitivesAndIsNotUndoable();
    std::cout << "Layer build step enable/disable action tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
