#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <core/BinarySerializer.h>
#include <core/Layer.h>
#include <core/LayerSelection.h>
#include <core/MeshPrimitive.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/WorldDataGenerator.h>
#include <core/WorldTriggerLine.h>
#include <core/WorldUpdateData.h>

namespace {

using bw::core::ComplexPolygon;
using bw::core::Layer;
using bw::core::LayerSelection;
using bw::core::MeshPrimitive;
using bw::core::Primitive;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ComplexPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

MeshPrimitive* makeRectangle(uint8_t priority) {
  auto* primitive = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      {rectangle(0.0f, 0.0f, 10.0f, 10.0f)});
  primitive->setPriority(priority);
  return primitive;
}

bw::core::WorldUpdateData updateData(
    wp::Vector2 const& position, LayerSelection const& layerSelection) {
  return {position, 0.0f, 0.0f, 0.0f, 0.0f, false, false, layerSelection};
}

// Adds a Layer holding one Primitive, and returns it.
Layer* addLayerWithPrimitive(
    bw::core::World& world, char const* name, uint8_t priority,
    Primitive** primitive = nullptr) {
  auto* layer = world.addLayer(name);
  auto* added = makeRectangle(priority);
  layer->addPrimitive(added);
  if (primitive) {
    *primitive = added;
  }
  return layer;
}

void theFoldGathersOnlyTheSelectedLayersPrimitives() {
  bw::core::World world(100.0f, 10.0f);
  auto* onActive = makeRectangle(1);
  world.addPrimitive(onActive);

  Primitive* onSelected{nullptr};
  auto* selected = addLayerWithPrimitive(world, "selected", 2, &onSelected);
  Primitive* onUnselected{nullptr};
  auto* unselected = addLayerWithPrimitive(world, "unselected", 3, &onUnselected);

  LayerSelection selection;
  selection.set(world.getActiveLayer()->getId());
  selection.set(selected->getId());

  auto const folded = bw::core::selectAndOrderPrimitives(world, selection);

  require(folded == std::vector<Primitive*>{onActive, onSelected},
          "the fold did not gather exactly the selected Layers' primitives");
  require(std::find(folded.begin(), folded.end(), onUnselected) == folded.end(),
          "the fold included a primitive from an unselected Layer");
  require(unselected->getNumPrimitives() == 1,
          "an unselected Layer lost its primitive");
}

void thePriorityFoldStaysNonLocalAcrossLayerBoundaries() {
  bw::core::World world(100.0f, 10.0f);

  // Authored so that ordering by Layer would disagree with ordering by
  // priority: the later Layer holds the lower priority.
  auto* late = makeRectangle(7);
  world.addPrimitive(late);
  Primitive* early{nullptr};
  auto* second = addLayerWithPrimitive(world, "second", 2, &early);
  Primitive* middle{nullptr};
  auto* third = addLayerWithPrimitive(world, "third", 5, &middle);

  auto selection = bw::core::SelectLayer(world.getActiveLayer()->getId());
  selection.set(second->getId());
  selection.set(third->getId());

  auto const folded = bw::core::selectAndOrderPrimitives(world, selection);

  require(folded == std::vector<Primitive*>{early, middle, late},
          "priority ordering did not run across the whole selected set");
}

void aPrimitiveFilterKeepsRejectedPrimitivesOutOfTheFold() {
  bw::core::World world(100.0f, 10.0f);
  auto* layer = world.getActiveLayer();

  auto* onFirstStep = makeRectangle(1);
  layer->addPrimitive(onFirstStep);

  auto laterStepIndex = layer->addStep(new bw::core::PrimitiveField());
  layer->setActiveStep(laterStepIndex);
  auto* onLaterStep = makeRectangle(2);
  layer->addPrimitive(onLaterStep);

  layer->setActiveStep(0);

  auto const selection = bw::core::SelectLayer(layer->getId());

  require(bw::core::selectAndOrderPrimitives(world, selection) ==
              std::vector<Primitive*>{onFirstStep, onLaterStep},
          "an unfiltered fold did not gather every step's primitives");

  // The rule behind the editor's "Show all steps' Primitives": nothing from a
  // step after the Layer's active one enters the fold, so a hidden Primitive
  // contributes no geometry either.
  auto const upToActiveStep = [](Layer const& owner, Primitive const* primitive) {
    auto owningStepIndex = owner.getOwningStepIndex(primitive);
    return owningStepIndex == ~0u ||
           owningStepIndex <= owner.getActiveStepIndex();
  };

  auto const folded =
      bw::core::selectAndOrderPrimitives(world, selection, upToActiveStep);

  require(folded == std::vector<Primitive*>{onFirstStep},
          "a filtered fold gathered a primitive its filter rejected");
  require(layer->getNumPrimitives() == 2,
          "filtering the fold removed a primitive from the Layer itself");
}

void aPrimitiveFilterSeesTheLayerThatOwnsEachPrimitive() {
  bw::core::World world(100.0f, 10.0f);
  auto* onActive = makeRectangle(1);
  world.addPrimitive(onActive);

  Primitive* onOther{nullptr};
  auto* other = addLayerWithPrimitive(world, "other", 2, &onOther);

  auto selection = bw::core::SelectLayer(world.getActiveLayer()->getId());
  selection.set(other->getId());

  auto const onlyOther = [](Layer const& owner, Primitive const*) {
    return owner.getName() == "other";
  };

  require(bw::core::selectAndOrderPrimitives(world, selection, onlyOther) ==
              std::vector<Primitive*>{onOther},
          "the filter did not receive the Layer owning each primitive");
}

void triggerLinesUseTheSameLayerIdSelection() {
  bw::core::World world(100.0f, 10.0f);
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      {9.0f, 15.0f}, {11.0f, 15.0f}));

  auto* selected = world.addLayer("selected");
  auto* selectedLine =
      new bw::core::WorldTriggerLine({9.0f, 15.0f}, {11.0f, 15.0f});
  selected->addTriggerLine(selectedLine);

  auto* unselected = world.addLayer("unselected");
  auto* unselectedLine =
      new bw::core::WorldTriggerLine({9.0f, 15.0f}, {11.0f, 15.0f});
  unselected->addTriggerLine(unselectedLine);

  LayerSelection selection;
  selection.set(world.getActiveLayer()->getId());
  selection.set(selected->getId());

  world.update(0.0f, updateData({10.0f, 10.0f}, selection), {100.0f, 100.0f});
  world.update(0.0f, updateData({10.0f, 20.0f}, selection), {100.0f, 100.0f});

  require(world.getTriggerLine(0)->getTotalTriggerCount() == 1,
          "a trigger line on the active, selected Layer was inactive");
  require(selectedLine->getTotalTriggerCount() == 1,
          "a trigger line on a selected non-active Layer was inactive");
  require(unselectedLine->getTotalTriggerCount() == 0,
          "a trigger line on an unselected Layer was checked");
}

void selectingEveryLayerKeepsEveryTriggerLineActive() {
  bw::core::World world(100.0f, 10.0f);
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      {9.0f, 15.0f}, {11.0f, 15.0f}));
  auto* other = world.addLayer("other");
  auto* otherLine =
      new bw::core::WorldTriggerLine({9.0f, 15.0f}, {11.0f, 15.0f});
  other->addTriggerLine(otherLine);

  auto const allLayers = bw::core::SelectAllLayers();
  world.update(0.0f, updateData({10.0f, 10.0f}, allLayers), {100.0f, 100.0f});
  world.update(0.0f, updateData({10.0f, 20.0f}, allLayers), {100.0f, 100.0f});

  require(world.getTriggerLine(0)->getTotalTriggerCount() == 1 &&
              otherLine->getTotalTriggerCount() == 1,
          "selecting every Layer disabled a trigger line");
}

void loadingAWorldRescopesTheSelectionToTheActiveLayer() {
  std::string const path = "world_layer_selection_tests.world";

  {
    bw::core::World world(100.0f, 10.0f);
    world.addPrimitive(makeRectangle(0));

    std::shared_ptr<bw::core::Serializer> writer(
        bw::core::BinarySerializer::toFile(path));
    bw::core::SerializationWorkData workData;
    world.serialize(writer, workData);
    writer->serialize();
  }

  bw::core::World loaded(100.0f, 10.0f);
  auto* extra = loaded.addLayer("extra");
  loaded.getWorldDataGenerator()->setLayerSelection(
      bw::core::SelectLayer(extra->getId()));
  require(loaded.getWorldDataGenerator()->getLayerSelection() !=
              bw::core::SelectLayer(loaded.getActiveLayer()->getId()),
          "the test did not establish a selection to be reset by loading");

  {
    std::shared_ptr<bw::core::Serializer> reader(
        bw::core::BinarySerializer::fromFile(path));
    reader->deserialize();
    bw::core::SerializationWorkData workData;
    workData.accelGridSize = 10.0f;
    require(loaded.deserialize(reader, workData),
            "a World failed to deserialize from a .world binary file");
  }

  require(loaded.getWorldDataGenerator()->getLayerSelection() ==
              bw::core::SelectLayer(loaded.getActiveLayer()->getId()),
          "loading a World did not rescope generation to its active Layer");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    theFoldGathersOnlyTheSelectedLayersPrimitives();
    thePriorityFoldStaysNonLocalAcrossLayerBoundaries();
    aPrimitiveFilterKeepsRejectedPrimitivesOutOfTheFold();
    aPrimitiveFilterSeesTheLayerThatOwnsEachPrimitive();
    triggerLinesUseTheSameLayerIdSelection();
    selectingEveryLayerKeepsEveryTriggerLineActive();
    loadingAWorldRescopesTheSelectionToTheActiveLayer();
    std::cout << "Generation and trigger collision select Layers by id\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
