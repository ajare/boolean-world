#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/PrefabField.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core-lua/CoreLua.h>
#include <core-lua/RunScript.h>

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

void addingARegisteredStepTypeAsOneUndoableAction() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(
      &document, "Add Layer Step",
      std::bind(editor::addLayerBuildStep, std::placeholders::_1, layer,
                "PrimitiveField"));

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

void prefabActionsCreateSelectRenameDeleteAndChangeTilingArguments() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto stepIndex = layer->addStep(new bw::core::DefinePrefabs());
  layer->setActiveStep(stepIndex);
  auto* step = static_cast<bw::core::DefinePrefabs*>(layer->getStep(stepIndex));

  require(editor::createPrefab(&document, layer, step),
          "Create Prefab action failed");
  require(step->getNumPrefabs() == 1 &&
              step->getPrefab(0)->getName() == "Prefab 1" &&
              step->getSelectedPrefab() == step->getPrefab(0),
          "Create Prefab did not generate a default name and select the result");

  auto* prefab = step->getSelectedPrefab();
  require(editor::renamePrefab(&document, layer, step, prefab, "Door") &&
              prefab->getName() == "Door",
          "Rename Prefab action failed");
  require(editor::setPrefabTileSize(
              &document, layer, step, prefab,
              bw::core::PrefabTileSize::Size128) &&
              prefab->getTileSize() == bw::core::PrefabTileSize::Size128,
          "Set Prefab tile size action failed");
  require(editor::setPrefabTags(
              &document, layer, step, prefab, {"door", "interior"}) &&
              prefab->getTags() ==
                  std::set<std::string>({"door", "interior"}),
          "Set Prefab tags action failed");
  require(editor::setPrefabTilingType(
              &document, layer, step, bw::core::PrefabTilingType::Square),
          "Set Prefab tiling type action failed");
  require(editor::deletePrefab(&document, layer, step, prefab) &&
              step->getNumPrefabs() == 0 && step->getSelectedPrefab() == nullptr,
          "Delete Prefab did not leave no Prefab selected");
}

void switchingPrefabsImmediatelyReplacesTheAuthoringFold() {
  editor::Document document;
  document.newDoc();
  editor::Settings settings;
  document.setPrimitiveFilter(
      [&settings](bw::core::Layer const& candidateLayer,
                  bw::core::Primitive const* primitive) {
        return editor::primitiveParticipatesInEditorFold(
            candidateLayer, primitive, settings);
      });
  auto* world = document.getWorld().get();
  auto* layer = world->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs;
  auto stepIndex = layer->addStep(definitions);
  layer->setActiveStep(stepIndex);

  auto* first = definitions->addPrefab("First");
  definitions->setSelectedPrefab(first);
  world->addPrimitive(makeRectangle(-100.0f));
  auto* second = definitions->addPrefab("Second");
  definitions->setSelectedPrefab(second);
  layer->rebuild();
  world->addPrimitive(makeRectangle(100.0f));

  auto* generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      world->getWorldDataGenerator());
  require(generator, "the editor World did not use its DynamicWorldDataGenerator");

  editor::selectPrefab(&document, layer, definitions, first);
  generator->getWorldData(world);
  auto firstFold = generator->getActiveClippingPrimitives();
  require(firstFold.size() == 1 && firstFold.front().bounds.getCentre().x < 0.0f,
          "selecting the first Prefab did not commit its isolated authoring fold");

  editor::selectPrefab(&document, layer, definitions, second);
  generator->getWorldData(world);
  auto secondFold = generator->getActiveClippingPrimitives();
  require(secondFold.size() == 1 && secondFold.front().bounds.getCentre().x > 0.0f,
          "selecting the second Prefab left the first Prefab's authoring fold rendered");
}

void prefabInstanceActionsUndoAndRefuseDeletingReferencedPrefabs() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* definitions = new bw::core::DefinePrefabs;
  layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("Used");
  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  layer->setActiveStep(fieldIndex);

  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 1, 2};
  auto undoBefore = editor::getUndoLevels();
  require(editor::transactUndoableActionAtomically(
              &document, "Place Prefab Instance",
              std::bind(editor::placePrefabInstance, std::placeholders::_1,
                        layer, field, tile)),
          "place Prefab instance action failed");
  require(editor::getUndoLevels() == undoBefore + 1 && field->getInstance(tile),
          "placing a Prefab instance was not exactly one undo entry");
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  field = static_cast<bw::core::PrefabField*>(layer->getStep(fieldIndex));
  require(!field->getInstance(tile), "undo did not remove the placed Prefab instance");
  editor::redo(&document);
  layer = document.getWorld()->getActiveLayer();
  definitions = static_cast<bw::core::DefinePrefabs*>(layer->getStep(1));
  field = static_cast<bw::core::PrefabField*>(layer->getStep(fieldIndex));
  prefab = definitions->getPrefab(0);
  require(field->getInstance(tile), "redo did not restore the Prefab instance");

  requireCoreException(
      [&] { editor::deletePrefab(&document, layer, definitions, prefab); },
      "deleting a referenced Prefab was not refused");

  layer->setActiveStep(fieldIndex);
  require(editor::transactUndoableActionAtomically(
              &document, "Clear Prefab Instance",
              std::bind(editor::clearPrefabInstance, std::placeholders::_1,
                        layer, field, tile)),
          "clear Prefab instance action failed");
  auto afterClear = editor::getUndoLevels();
  require(!editor::transactUndoableActionAtomically(
              &document, "Clear Prefab Instance",
              std::bind(editor::clearPrefabInstance, std::placeholders::_1,
                        layer, field, tile)) &&
              editor::getUndoLevels() == afterClear,
          "clearing an empty Tile created an undo entry");
}

void prefabFieldBindingRefusesRemovingItsDefinitionsThroughActions() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();
  auto* boundDefinitions = new bw::core::DefinePrefabs;
  layer->addStep(boundDefinitions);
  auto* unboundDefinitions = new bw::core::DefinePrefabs;
  layer->addStep(unboundDefinitions);
  auto* field = new bw::core::PrefabField;
  layer->addStep(field);

  require(editor::bindPrefabField(&document, layer, field, boundDefinitions) &&
              field->getDefinePrefabsStepId() == boundDefinitions->getId(),
          "PrefabField bind action did not retain the selected DefinePrefabs step");
  requireCoreException(
      [&] { editor::removeLayerBuildStep(&document, layer, 1); },
      "removing a DefinePrefabs step bound by a PrefabField action was not refused");
  require(layer->getNumSteps() == 4,
          "a refused DefinePrefabs removal disturbed the Layer's step list");

  require(editor::removeLayerBuildStep(&document, layer, 2) && layer->getNumSteps() == 3,
          "removing an unbound DefinePrefabs step through actions did not succeed");
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

void movingAPrimitiveBetweenStepsOfTheSameTypeIsOneUndoableAction() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  auto* moved = makeRectangle(1.0f);
  document.getWorld()->addPrimitive(moved);
  auto const secondIndex = layer->addStep(makeField({10.0f}));

  require(layer->getOwningStepIndex(moved) == 0,
          "the fixture Primitive did not start in step 0");
  require(comesBefore(*layer, 1.0f, 10.0f),
          "the fixture did not fold the step 0 Primitive before the step 1 one");

  // Step 0 also holds the newDoc ghost Primitive, so the first step's
  // occupancy is checked as a delta rather than against zero.
  auto const fieldCountBefore = layer->getPrimitiveField()->getNumPrimitives();
  document.setModified(false);
  auto undoBefore = editor::getUndoLevels();

  editor::transactUndoableAction(
      &document, "Move Primitive to Layer Step",
      std::bind(editor::movePrimitiveToLayerBuildStep, std::placeholders::_1,
                layer, moved, secondIndex));

  require(editor::getUndoLevels() == undoBefore + 1,
          "moving a Primitive between steps did not create exactly one undo entry");
  layer = document.getWorld()->getActiveLayer();
  require(!layer->getPrimitiveField()->ownsPrimitive(moved) &&
              layer->getPrimitiveField()->getNumPrimitives() == fieldCountBefore - 1,
          "the moved Primitive was left behind in its old step");
  require(layer->getOwningStepIndex(moved) == secondIndex,
          "the moved Primitive is not attributed to its new step");
  require(comesBefore(*layer, 10.0f, 1.0f),
          "the moved Primitive did not fold in its new step's place");
  require(document.isModified(),
          "moving a Primitive between steps did not mark the document modified");
  require(document.getSelectedPrimitiveIndices().size() == 1 &&
              layer->getPrimitive(*document.getSelectedPrimitiveIndices().begin()) == moved,
          "the action did not follow the moved Primitive to its new index");

  // Undo restores the World from a snapshot, so `moved` is a dangling pointer
  // from here on and the Layer is checked by what it derives instead.
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getPrimitiveField()->getNumPrimitives() == fieldCountBefore,
          "undo did not return the Primitive to its original step");
  require(comesBefore(*layer, 1.0f, 10.0f),
          "undo did not restore the original fold order");
  require(!document.isModified(), "undo did not restore the clean modified state");

  editor::redo(&document);
  layer = document.getWorld()->getActiveLayer();
  require(layer->getPrimitiveField()->getNumPrimitives() == fieldCountBefore - 1,
          "redo did not re-apply the move");
  require(comesBefore(*layer, 10.0f, 1.0f),
          "redo did not restore the moved fold order");
  require(document.isModified(), "redo did not restore the modified state");
}

void runScriptCanBeAddedAndItsAuthoredStateIsUndoable(
    bw::core::ScriptRuntime& runtime) {
  editor::clearUndoHistory();
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  editor::transactUndoableAction(
      &document, "Add RunScript Step",
      std::bind(editor::addLayerBuildStep, std::placeholders::_1, layer,
                "RunScript"));
  layer = document.getWorld()->getActiveLayer();
  require(layer->getNumSteps() == 2 &&
              layer->getStep(1)->getType() == "RunScript",
          "adding a registered RunScript did not put it in the Layer recipe");

  bw::core::StepVariableDefinition density;
  density.name = "density";
  density.type = bw::core::BuildVariableType::Integer;
  density.defaultValue = int64_t{5};
  density.integerMinimum = 1;
  density.integerMaximum = 10;
  runtime.load(
      "scatter", "-- an empty successful build script", {}, {density});
  auto* step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  auto undoBefore = editor::getUndoLevels();
  editor::transactUndoableAction(
      &document, "Select Script",
      std::bind(editor::setRunScriptScriptName, std::placeholders::_1, layer,
                step, "scatter"));
  editor::transactUndoableAction(
      &document, "Set Seed",
      std::bind(editor::setRunScriptSeed, std::placeholders::_1, layer, step,
                uint64_t{42}));
  editor::transactUndoableAction(
      &document, "Set Step Name",
      std::bind(editor::setLayerBuildStepName, std::placeholders::_1, layer,
                uint32_t{1}, "rocks"));
  editor::transactUndoableAction(
      &document, "Set Extra Resources",
      std::bind(editor::setRunScriptExtraResourceNames,
                std::placeholders::_1, layer, step,
                std::vector<std::string>{"OreImage", "/SharedData"}));
  editor::transactUndoableAction(
      &document, "Set Param",
      std::bind(editor::setRunScriptStepVariableValue,
                std::placeholders::_1, layer, step, "density",
                bw::core::BuildVariableValue{int64_t{8}}));

  require(editor::getUndoLevels() == undoBefore + 5,
          "RunScript state edits did not each create one undo entry");
  require(step->getScriptName() == "scatter" && step->getSeed() == 42 &&
              step->getName() == "rocks" &&
              step->getExtraResourceNames() ==
                  std::vector<std::string>{"OreImage", "/SharedData"} &&
              std::get<int64_t>(step->getStepVariableValues().at("density")) == 8 &&
              !step->hasFailed(),
          "RunScript state actions did not update and rebuild the step");

  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(
      std::get<int64_t>(step->getStepVariableValues().at("density")) == 5,
      "undo did not restore the RunScript resource default");
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(step->getExtraResourceNames().empty(),
          "undo did not restore the RunScript extra-resources list");
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(step->getName().empty(),
          "undo did not restore the RunScript step name");
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(step->getSeed() == 0,
          "undo did not restore the RunScript seed");
  editor::undo(&document);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(
      step->getScriptName() == bw::core::defaultLayerBuildStepScriptName &&
          !step->hasFailed(),
      "undo did not restore the RunScript default script reference");

  editor::redo(&document, 5);
  layer = document.getWorld()->getActiveLayer();
  step = static_cast<bw::core::RunScript*>(layer->getStep(1));
  require(step->getScriptName() == "scatter" && step->getSeed() == 42 &&
              step->getName() == "rocks" &&
              step->getExtraResourceNames() ==
                  std::vector<std::string>{"OreImage", "/SharedData"} &&
              std::get<int64_t>(step->getStepVariableValues().at("density")) == 8 &&
              !step->hasFailed(),
          "redo did not restore all authored RunScript state");
}

void buildVariableActionsAreUndoableAtWorldAndLayerScopes() {
  editor::Document document;
  document.newDoc();
  editor::clearUndoHistory();

  auto world = document.getWorld();
  auto* layer = world->getActiveLayer();
  editor::transact(&document, editor::CommandId::SetWorldBuildVariable, [&] {
    editor::setWorldBuildVariable(&document, "count", int64_t{4});
  });
  editor::transact(&document, editor::CommandId::SetLayerBuildVariable, [&] {
    editor::setLayerBuildVariable(&document, layer, "count", int64_t{7});
  });
  require(std::get<int64_t>(layer->getEffectiveBuildVariables().at("count")) == 7,
          "Layer build-variable action did not override the World value");

  editor::undo(&document);
  world = document.getWorld();
  layer = world->getActiveLayer();
  require(std::get<int64_t>(layer->getEffectiveBuildVariables().at("count")) == 4 &&
              layer->getBuildVariables().empty(),
          "undo did not remove the Layer build-variable override");

  editor::undo(&document);
  require(document.getWorld()->getBuildVariables().empty(),
          "undo did not remove the World build variable");
}

void movingAPrimitiveIntoAStepOfAnotherTypeIsRejectedThroughTheAction() {
  editor::Document document;
  document.newDoc();
  auto* layer = document.getWorld()->getActiveLayer();

  auto* authored = makeRectangle(2.0f);
  document.getWorld()->addPrimitive(authored);

  auto const prefabsIndex = layer->addStep(new bw::core::DefinePrefabs());
  auto* prefabs =
      static_cast<bw::core::DefinePrefabs*>(layer->getStep(prefabsIndex));
  prefabs->setSelectedPrefab(prefabs->addPrefab("Target"));
  layer->rebuild();

  require(!layer->canMovePrimitiveToStep(authored, prefabsIndex),
          "a PrimitiveField Primitive was reported movable into a DefinePrefabs step");
  requireCoreException(
      [&] {
        editor::movePrimitiveToLayerBuildStep(
            &document, layer, authored, prefabsIndex);
      },
      "moving a Primitive into a step of another type was not rejected");
  require(layer->getPrimitiveField()->ownsPrimitive(authored),
          "a rejected move disturbed the Primitive's original ownership");
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    bw::core::ScriptRuntime runtime;
    bw::core::registerScriptStepTypes(runtime);

    disablingStepZeroRemovesItsPrimitivesAndRebuildRestoresThemOnReEnable();
    togglingStepEnabledIsOneUndoableActionThatRestoresLayerState();
    addingARegisteredStepTypeAsOneUndoableAction();
    removingANonFirstStepIsOneUndoableActionAndTheFirstStepIsRejected();
    movingAStepIsOneUndoableActionAndMovesIntoOrOutOfIndexZeroAreRejected();
    prefabActionsCreateSelectRenameDeleteAndChangeTilingArguments();
    switchingPrefabsImmediatelyReplacesTheAuthoringFold();
    prefabInstanceActionsUndoAndRefuseDeletingReferencedPrefabs();
    prefabFieldBindingRefusesRemovingItsDefinitionsThroughActions();
    selectingTheActiveStepRedirectsCreatedPrimitivesAndIsNotUndoable();
    movingAPrimitiveBetweenStepsOfTheSameTypeIsOneUndoableAction();
    runScriptCanBeAddedAndItsAuthoredStateIsUndoable(runtime);
    buildVariableActionsAreUndoableAtWorldAndLayerScopes();
    movingAPrimitiveIntoAStepOfAnotherTypeIsRejectedThroughTheAction();
    std::cout << "Layer build step editor action tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
