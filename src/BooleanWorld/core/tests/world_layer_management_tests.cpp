#include <iostream>
#include <stdexcept>

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

}  // namespace

int main() {
  try {
    addLayerAppendsANamedLayerWithAFreshStableId();
    removingALayerDoesNotRenumberTheSurvivors();
    removingTheLastLayerIsRejected();
    removingTheActiveLayerFallsBackWithoutChangingSurvivorIds();
    reorderingLayersPreservesIdsAndTheActiveLayerIdentity();
    std::cout << "World's Layer collection supports add/remove/reorder with stable ids and a tracked active Layer\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
