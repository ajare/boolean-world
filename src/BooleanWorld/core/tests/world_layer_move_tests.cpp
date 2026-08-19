#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <core/CoreException.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makePrimitive(float x) {
  auto primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setPosition({x, 0.0f});
  primitive->setSize(2.0f, 2.0f);
  return primitive;
}

bool layerFindsPrimitive(bw::core::Layer const& layer, bw::core::Primitive const* primitive) {
  auto const found = layer.findPrimitives(primitive->getBounds());
  return std::find(found.begin(), found.end(), primitive) != found.end();
}

bool layerFindsTriggerLine(bw::core::Layer const& layer, bw::core::WorldTriggerLine const* triggerLine) {
  auto const found = layer.findTriggerLines(triggerLine->getBounds());
  return std::find(found.begin(), found.end(), triggerLine) != found.end();
}

void movingAPrimitiveTransfersLayerOwnership() {
  bw::core::World world(100.0f, 10.0f);
  auto* source = world.getActiveLayer();
  auto* destination = world.addLayer("destination");

  auto* stayed = makePrimitive(-20.0f);
  auto* moved = makePrimitive(0.0f);
  world.addPrimitive(stayed);
  world.addPrimitive(moved);

  world.movePrimitiveToLayer(moved, destination);

  require(source->getNumPrimitives() == 1 && source->getPrimitive(0) == stayed,
          "the source Layer did not lose exactly the moved Primitive");
  require(destination->getNumPrimitives() == 1 && destination->getPrimitive(0) == moved,
          "the destination Layer did not gain the moved Primitive");
  require(!layerFindsPrimitive(*source, moved),
          "the source Layer's acceleration grid still reported the moved Primitive");
  require(layerFindsPrimitive(*destination, moved),
          "the destination Layer's acceleration grid did not register the moved Primitive");
  require(layerFindsPrimitive(*source, stayed),
          "moving a Primitive corrupted the source Layer's remaining acceleration grid entries");
}

void movingCompactsTheSourceLayersRemainingIds() {
  bw::core::World world(100.0f, 10.0f);
  auto* source = world.getActiveLayer();
  auto* destination = world.addLayer("destination");

  auto* first = makePrimitive(-20.0f);
  auto* moved = makePrimitive(-10.0f);
  auto* third = makePrimitive(0.0f);
  world.addPrimitive(first);
  world.addPrimitive(moved);
  world.addPrimitive(third);

  world.movePrimitiveToLayer(moved, destination);

  require(source->getNumPrimitives() == 2, "the source Layer retained the wrong Primitive count");
  require(source->getPrimitive(0) == first && source->getPrimitive(0)->getId() == 0,
          "the source Layer's first remaining Primitive did not keep id 0");
  require(source->getPrimitive(1) == third && source->getPrimitive(1)->getId() == 1,
          "the source Layer did not compact the id behind the moved Primitive");
  require(destination->getPrimitive(0)->getId() == 0,
          "the destination Layer did not assign the moved Primitive a fresh id");
}

void movingIntoALayerWithExistingContentPreservesIt() {
  bw::core::World world(100.0f, 10.0f);
  auto* source = world.getActiveLayer();
  auto* destination = world.addLayer("destination");

  auto* resident = makePrimitive(50.0f);
  destination->addPrimitive(resident);

  auto* moved = makePrimitive(0.0f);
  world.addPrimitive(moved);

  world.movePrimitiveToLayer(moved, destination);

  require(destination->getNumPrimitives() == 2,
          "moving into a populated Layer changed its existing Primitive count incorrectly");
  require(destination->getPrimitive(0) == resident && resident->getId() == 0,
          "moving into a populated Layer disturbed its resident Primitive's id");
  require(destination->getPrimitive(1) == moved && moved->getId() == 1,
          "the moved Primitive was not appended after the destination Layer's existing content");
  require(layerFindsPrimitive(*destination, resident) && layerFindsPrimitive(*destination, moved),
          "the destination Layer's acceleration grid did not register both Primitives");
  require(source->getNumPrimitives() == 0, "the source Layer retained the moved Primitive");
}

void movingAnUnownedPrimitiveThrows() {
  bw::core::World world(100.0f, 10.0f);
  auto* destination = world.addLayer("destination");
  auto detached = std::unique_ptr<bw::core::Primitive>(makePrimitive(0.0f));

  bool threw{false};
  try {
    world.movePrimitiveToLayer(detached.get(), destination);
  } catch (bw::core::CoreException const&) {
    threw = true;
  }

  require(threw, "moving a Primitive owned by no Layer of the World did not fail clearly");
}

void movingATriggerLineTransfersLayerOwnershipAndCollisionBehavior() {
  bw::core::World world(100.0f, 10.0f);
  auto* source = world.getActiveLayer();
  auto* destination = world.addLayer("destination");

  auto* triggerLine = new bw::core::WorldTriggerLine({-1.0f, 0.0f}, {1.0f, 0.0f});
  world.addTriggerLine(triggerLine);

  world.moveTriggerLineToLayer(triggerLine, destination);

  require(source->getNumTriggerLines() == 0,
          "the source Layer retained the moved WorldTriggerLine");
  require(destination->getNumTriggerLines() == 1 && destination->getTriggerLine(0) == triggerLine,
          "the destination Layer did not gain the moved WorldTriggerLine");
  require(!layerFindsTriggerLine(*source, triggerLine),
          "the source Layer's acceleration grid still reported the moved WorldTriggerLine");
  require(layerFindsTriggerLine(*destination, triggerLine),
          "the destination Layer's acceleration grid did not register the moved WorldTriggerLine");

  // The accelerated collision sweep must answer from wherever the trigger
  // line now actually lives, not where it used to.
  require(triggerLine->checkCollide({0.0f, -1.0f}, {0.0f, 1.0f}, 0.0f),
          "a moved WorldTriggerLine stopped detecting collisions after the move");
}

void movingATriggerLineCompactsTheSourceLayersRemainingIds() {
  bw::core::World world(100.0f, 10.0f);
  auto* source = world.getActiveLayer();
  auto* destination = world.addLayer("destination");

  auto* first = new bw::core::WorldTriggerLine({-20.0f, -1.0f}, {-20.0f, 1.0f});
  auto* moved = new bw::core::WorldTriggerLine({-10.0f, -1.0f}, {-10.0f, 1.0f});
  auto* third = new bw::core::WorldTriggerLine({0.0f, -1.0f}, {0.0f, 1.0f});
  world.addTriggerLine(first);
  world.addTriggerLine(moved);
  world.addTriggerLine(third);

  world.moveTriggerLineToLayer(moved, destination);

  require(source->getNumTriggerLines() == 2, "the source Layer retained the wrong WorldTriggerLine count");
  require(source->getTriggerLine(0) == first && first->getId() == 0,
          "the source Layer's first remaining WorldTriggerLine did not keep id 0");
  require(source->getTriggerLine(1) == third && third->getId() == 1,
          "the source Layer did not compact the id behind the moved WorldTriggerLine");
  require(destination->getTriggerLine(0)->getId() == 0,
          "the destination Layer did not assign the moved WorldTriggerLine a fresh id");
}

void movingAnUnownedTriggerLineThrows() {
  bw::core::World world(100.0f, 10.0f);
  auto* destination = world.addLayer("destination");
  auto detached = std::unique_ptr<bw::core::WorldTriggerLine>(
      new bw::core::WorldTriggerLine({-1.0f, 0.0f}, {1.0f, 0.0f}));

  bool threw{false};
  try {
    world.moveTriggerLineToLayer(detached.get(), destination);
  } catch (bw::core::CoreException const&) {
    threw = true;
  }

  require(threw, "moving a WorldTriggerLine owned by no Layer of the World did not fail clearly");
}

}  // namespace

int main() {
  try {
    movingAPrimitiveTransfersLayerOwnership();
    movingCompactsTheSourceLayersRemainingIds();
    movingIntoALayerWithExistingContentPreservesIt();
    movingAnUnownedPrimitiveThrows();
    movingATriggerLineTransfersLayerOwnershipAndCollisionBehavior();
    movingATriggerLineCompactsTheSourceLayersRemainingIds();
    movingAnUnownedTriggerLineThrows();
    std::cout << "Moving Primitives and WorldTriggerLines between Layers preserves acceleration-grid integrity\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
