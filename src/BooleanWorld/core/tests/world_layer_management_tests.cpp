#include <iostream>
#include <stdexcept>

#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void addLayerAppendsANamedLayerWithAFreshStableId() {
  bw::core::World world(100.0f, 10.0f);

  auto layer1 = world.addLayer("Midground");
  auto layer2 = world.addLayer("Background");

  require(world.getNumLayers() == 3, "addLayer did not grow the World's Layer collection");
  require(layer1->getId() != 0 && layer2->getId() != 0 && layer1->getId() != layer2->getId(),
          "addLayer did not assign fresh, distinct ids");
  require(layer1->getName() == "Midground", "addLayer did not preserve the given name");
  require(world.getLayers()[1] == layer1 && world.getLayers()[2] == layer2,
          "addLayer did not append the new Layer at the end of the collection");
}

void removingALayerDoesNotRenumberTheSurvivors() {
  bw::core::World world(100.0f, 10.0f);

  auto layer1 = world.addLayer("Midground");
  auto layer2 = world.addLayer("Background");

  auto layer1Id = layer1->getId();
  auto layer2Id = layer2->getId();

  world.removeLayer(layer1);

  require(world.getNumLayers() == 2, "removeLayer did not shrink the World's Layer collection");
  require(world.getLayers()[1]->getId() == layer2Id,
          "removing a Layer changed a surviving Layer's id");
  require(world.getLayers()[1] == layer2, "removing a Layer left the wrong Layer in the collection");
  require(world.getLayers()[0]->getId() == 0, "removing a Layer changed the original Layer's id");
  (void)layer1Id;
}

void removingTheLastLayerIsRejected() {
  bw::core::World world(100.0f, 10.0f);

  bool threw = false;
  try {
    world.removeLayer(world.getLayers()[0]);
  } catch (std::exception const&) {
    threw = true;
  }

  require(threw, "removing a World's only Layer did not throw");
  require(world.getNumLayers() == 1, "removing a World's only Layer changed its Layer count");
}

void removingTheActiveLayerFallsBackWithoutChangingSurvivorIds() {
  bw::core::World world(100.0f, 10.0f);

  auto layer1 = world.addLayer("Midground");
  world.addLayer("Background");

  require(world.getActiveLayerIndex() == 0, "a freshly extended World's active index moved unexpectedly");

  world.removeLayer(world.getLayers()[0]);

  require(world.getActiveLayerIndex() == 0, "removing the active Layer did not fall back to a valid index");
  require(world.getLayers()[0] == layer1, "removing the active Layer left the wrong Layer at index 0");
  require(world.getLayers()[0]->getId() == layer1->getId(),
          "removing the active Layer changed a surviving Layer's id");
}

void reorderingLayersPreservesIdsAndTheActiveLayerIdentity() {
  bw::core::World world(100.0f, 10.0f);

  auto layer0 = world.getLayers()[0];
  auto layer1 = world.addLayer("Midground");
  auto layer2 = world.addLayer("Background");

  // Make layer1 (index 1) the active Layer before reordering it away.
  world.moveLayer(1, 0);
  require(world.getLayers()[0] == layer1, "moveLayer did not move the Layer to its target index");
  require(world.getLayers()[1] == layer0, "moveLayer did not shift the displaced Layer");
  require(world.getLayers()[2] == layer2, "moveLayer disturbed an untouched Layer's position");

  require(layer0->getId() == 0, "reordering changed the original Layer's id");
  require(layer1->getId() != 0 && layer1->getId() != layer2->getId(),
          "reordering changed a Layer's id");

  // The active Layer was layer0 (index 0) before the move; it is now at
  // index 1, and moveLayer must have followed it there.
  require(world.getActiveLayerIndex() == 1,
          "reordering Layers changed which Layer was active instead of tracking its new position");
  require(world.getActiveLayer() == layer0, "reordering Layers lost track of the active Layer's identity");
}

void importingALayerWithANonCollidingIdPreservesIt() {
  bw::core::World world(100.0f, 10.0f);

  auto* imported = new bw::core::Layer(41, "Imported", 100.0f, 10.0f);

  auto* added = world.addLayer(imported);

  require(added == imported, "addLayer(Layer*) did not return the appended Layer");
  require(added->getId() == 41, "addLayer(Layer*) changed a non-colliding imported id");
  require(world.getNumLayers() == 2, "addLayer(Layer*) did not grow the World's Layer collection");
  require(world.getLayers()[1] == imported, "addLayer(Layer*) did not append at the end of the collection");

  // A later addLayer(name) must not collide with the imported id.
  auto* next = world.addLayer("Next");
  require(next->getId() != 41, "a subsequently added Layer collided with an imported id");
}

void importingALayerWithACollidingIdIsReassignedAFreshOne() {
  bw::core::World world(100.0f, 10.0f);

  // World's own default Layer already has id 0; importing another Layer
  // that also claims id 0 must not collide with it.
  auto* imported = new bw::core::Layer(0, "Imported", 100.0f, 10.0f);
  auto* rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  imported->addPrimitive(rect);

  auto* added = world.addLayer(imported);

  require(added->getId() != 0, "addLayer(Layer*) did not reassign a colliding imported id");
  require(added->getNumPrimitives() == 1 && added->getPrimitive(0) == rect,
          "addLayer(Layer*) lost the imported Layer's content while reassigning its id");
  require(world.getLayers()[0]->getId() == 0,
          "addLayer(Layer*) disturbed the World's existing Layer's id");
}

void getLayerFindsAnExistingIdAndReportsAMissingOneAsNull() {
  bw::core::World world(100.0f, 10.0f);
  auto* second = world.addLayer("Second");

  require(world.getLayer(0) == world.getLayers()[0], "getLayer did not find the default Layer by id");
  require(world.getLayer(second->getId()) == second, "getLayer did not find an added Layer by id");
  require(world.getLayer(999) == nullptr, "getLayer did not report a missing id as null");

  bw::core::World const& constWorld = world;
  require(constWorld.getLayer(second->getId()) == second, "the const overload of getLayer did not find an added Layer");
}

void setActiveLayerUpdatesFocusAndRejectsAForeignLayer() {
  bw::core::World world(100.0f, 10.0f);

  auto layer1 = world.addLayer("Midground");
  world.addLayer("Background");

  world.setActiveLayer(layer1);
  require(world.getActiveLayerIndex() == 1, "setActiveLayer did not move focus to the given Layer");
  require(world.getActiveLayer() == layer1, "setActiveLayer did not track the given Layer's identity");

  world.setActiveLayer(world.getLayers()[0]);
  require(world.getActiveLayerIndex() == 0, "setActiveLayer did not move focus back to the first Layer");

  bw::core::Layer foreign(999, "Foreign", 100.0f, 10.0f);
  bool threw = false;
  try {
    world.setActiveLayer(&foreign);
  } catch (std::exception const&) {
    threw = true;
  }

  require(threw, "setActiveLayer accepted a Layer that does not belong to this World");
  require(world.getActiveLayerIndex() == 0, "a rejected setActiveLayer call moved focus anyway");
}

}  // namespace

int main() {
  try {
    addLayerAppendsANamedLayerWithAFreshStableId();
    removingALayerDoesNotRenumberTheSurvivors();
    removingTheLastLayerIsRejected();
    removingTheActiveLayerFallsBackWithoutChangingSurvivorIds();
    reorderingLayersPreservesIdsAndTheActiveLayerIdentity();
    importingALayerWithANonCollidingIdPreservesIt();
    importingALayerWithACollidingIdIsReassignedAFreshOne();
    getLayerFindsAnExistingIdAndReportsAMissingOneAsNull();
    setActiveLayerUpdatesFocusAndRejectsAForeignLayer();
    std::cout << "World's Layer collection supports add/remove/reorder with stable ids and a tracked active Layer\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
