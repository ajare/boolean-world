#include <iostream>
#include <stdexcept>

#include <core/LayerSelection.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/WorldUpdateData.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::WorldUpdateData updateData(
    wp::Vector2 const& position,
    bw::core::LayerSelection const& layerSelection) {
  return {position, 0.0f, 0.0f, 0.0f, 0.0f, false, false, layerSelection};
}

void triggerLinesUseTheWholeLayerSelection() {
  bw::core::World world(100.0f, 10.0f);
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {9.0f, 15.0f}, {11.0f, 15.0f}));
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      2, {9.0f, 15.0f}, {11.0f, 15.0f}));
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      3, {9.0f, 15.0f}, {11.0f, 15.0f}));
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      BW_LAYER_ALL, {9.0f, 15.0f}, {11.0f, 15.0f}));

  bw::core::LayerSelection selectedLayers;
  selectedLayers.set(0);
  selectedLayers.set(2);

  world.update(0.0f, updateData({10.0f, 10.0f}, selectedLayers),
               {100.0f, 100.0f});
  world.update(0.0f, updateData({10.0f, 20.0f}, selectedLayers),
               {100.0f, 100.0f});

  require(world.getTriggerLine(0)->getTotalTriggerCount() == 1,
          "a trigger line on the first selected layer was inactive");
  require(world.getTriggerLine(1)->getTotalTriggerCount() == 1,
          "a trigger line on another selected layer was inactive");
  require(world.getTriggerLine(2)->getTotalTriggerCount() == 0,
          "a trigger line on an unselected layer was active");
  require(world.getTriggerLine(3)->getTotalTriggerCount() == 1,
          "an all-layer trigger line was inactive");
}

void allLayerSelectionKeepsTriggerLinesActive() {
  bw::core::World world(100.0f, 10.0f);
  world.addTriggerLine(new bw::core::WorldTriggerLine(
      17, {9.0f, 15.0f}, {11.0f, 15.0f}));

  auto allLayers = bw::core::SelectLayer(BW_LAYER_ALL);
  world.update(0.0f, updateData({10.0f, 10.0f}, allLayers),
               {100.0f, 100.0f});
  world.update(0.0f, updateData({10.0f, 20.0f}, allLayers),
               {100.0f, 100.0f});

  require(world.getTriggerLine(0)->getTotalTriggerCount() == 1,
          "selecting all layers disabled a layer-specific trigger line");
}

}  // namespace

int main() {
  try {
    triggerLinesUseTheWholeLayerSelection();
    allLayerSelectionKeepsTriggerLinesActive();
    std::cout << "Trigger lines respect multi-layer selections\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
