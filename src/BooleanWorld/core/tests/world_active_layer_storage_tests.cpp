#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makeRectangle() {
  auto* rectangle = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rectangle->setSize(10.0f, 10.0f);
  return rectangle;
}

void primitivesAddedViaTheFacadeLandInTheActiveLayer() {
  bw::core::World world(100.0f, 10.0f);
  auto* other = world.addLayer("other");

  auto* primitive = makeRectangle();
  world.addPrimitive(primitive);

  auto* active = world.getActiveLayer();
  require(active->getNumPrimitives() == 1 && active->getPrimitive(0) == primitive,
          "a primitive added through World's facade did not land in the active Layer");
  require(other->getNumPrimitives() == 0,
          "a primitive added through World's facade leaked into an inactive Layer");
  require(world.getNumPrimitives() == 1 && world.getPrimitive(0) == primitive,
          "World's facade did not read back the active Layer's primitive");
}

void triggerLinesAddedViaTheFacadeLandInTheActiveLayer() {
  bw::core::World world(100.0f, 10.0f);
  auto* other = world.addLayer("other");

  auto* triggerLine = new bw::core::WorldTriggerLine(
      0, {10.0f, 20.0f}, {30.0f, 40.0f});
  world.addTriggerLine(triggerLine);

  auto* active = world.getActiveLayer();
  require(active->getNumTriggerLines() == 1 && active->getTriggerLine(0) == triggerLine,
          "a trigger line added through World's facade did not land in the active Layer");
  require(other->getNumTriggerLines() == 0,
          "a trigger line added through World's facade leaked into an inactive Layer");
  require(world.getNumTriggerLines() == 1 && world.getTriggerLine(0) == triggerLine,
          "World's facade did not read back the active Layer's trigger line");
}

void removalThroughTheFacadeEmptiesTheActiveLayer() {
  bw::core::World world(100.0f, 10.0f);
  auto* primitive = makeRectangle();
  world.addPrimitive(primitive);
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {10.0f, 20.0f}, {30.0f, 40.0f}));

  world.removePrimitive(primitive);
  world.removeTriggerLine(0u);

  auto* active = world.getActiveLayer();
  require(active->getNumPrimitives() == 0,
          "removing a primitive through World's facade did not empty the active Layer");
  require(active->getNumTriggerLines() == 0,
          "removing a trigger line through World's facade did not empty the active Layer");
}

void theActiveLayerAlsoBacksTheFacadeAccelerationGrids() {
  bw::core::World world(100.0f, 10.0f);
  auto* primitive = makeRectangle();
  world.addPrimitive(primitive);

  auto const* active = world.getActiveLayer();
  auto const layerHits = active->findPrimitives(primitive->getBounds());
  require(std::find(layerHits.begin(), layerHits.end(), primitive) != layerHits.end(),
          "a primitive added through World's facade was missing from the active Layer's lookup grid");
  require(world.findPrimitives(primitive->getBounds()).size() == layerHits.size(),
          "World's spatial query did not answer from the active Layer's lookup grid");
}

}  // namespace

int main() {
  try {
    primitivesAddedViaTheFacadeLandInTheActiveLayer();
    triggerLinesAddedViaTheFacadeLandInTheActiveLayer();
    removalThroughTheFacadeEmptiesTheActiveLayer();
    theActiveLayerAlsoBacksTheFacadeAccelerationGrids();
    std::cout << "World's facade stores content in the active Layer\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
