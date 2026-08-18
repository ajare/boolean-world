#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/LayerSelection.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/WorldUpdateData.h>

namespace {

using bw::core::LayerSelection;
using bw::core::World;
using bw::core::WorldTriggerLine;
using bw::core::WorldTriggerLineSide;
using bw::core::WorldUpdateData;

struct TriggerLineSpec {
  uint8_t layer;
  wp::Vector2 p0;
  wp::Vector2 p1;
  WorldTriggerLineSide side{WorldTriggerLineSide::Both};
};

using TriggerCounts = std::array<uint32_t, 3>;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

WorldUpdateData updateData(
    wp::Vector2 const& position,
    float radius,
    LayerSelection const& layerSelection) {
  return {position, 0.0f, radius, 0.0f, 0.0f, false, false, layerSelection};
}

bool layerIsSelected(WorldTriggerLine const& triggerLine,
                     LayerSelection const& layerSelection) {
  auto const layer = triggerLine.getLayer();
  return layer == BW_LAYER_ALL || layerSelection.test(size_t(layer));
}

std::vector<TriggerCounts> runAcceleratedAndExhaustive(
    std::vector<TriggerLineSpec> const& specs,
    std::vector<wp::Vector2> const& positions,
    float radius,
    LayerSelection const& layerSelection) {
  World world(1000.0f, 50.0f);
  std::vector<std::unique_ptr<WorldTriggerLine>> exhaustiveLines;
  exhaustiveLines.reserve(specs.size());

  for (auto const& spec : specs) {
    world.addTriggerLine(new WorldTriggerLine(spec.layer, spec.p0, spec.p1, spec.side));
    exhaustiveLines.push_back(
        std::make_unique<WorldTriggerLine>(spec.layer, spec.p0, spec.p1, spec.side));
  }

  for (size_t i = 0; i < positions.size(); ++i) {
    world.update(0.0f, updateData(positions[i], radius, layerSelection),
                 {1000.0f, 1000.0f});

    if (i == 0) {
      continue;
    }

    for (auto const& triggerLine : exhaustiveLines) {
      if (layerIsSelected(*triggerLine, layerSelection)) {
        triggerLine->checkCollide(positions[i - 1], positions[i], radius);
      }
    }
  }

  std::vector<TriggerCounts> counts;
  counts.reserve(specs.size());
  for (size_t i = 0; i < specs.size(); ++i) {
    auto const* accelerated = world.getTriggerLine(uint32_t(i));
    auto const* exhaustive = exhaustiveLines[i].get();
    auto const acceleratedCounts = TriggerCounts{
        accelerated->getTriggerCount(WorldTriggerLineSide::Red),
        accelerated->getTriggerCount(WorldTriggerLineSide::Blue),
        accelerated->getTotalTriggerCount()};
    auto const exhaustiveCounts = TriggerCounts{
        exhaustive->getTriggerCount(WorldTriggerLineSide::Red),
        exhaustive->getTriggerCount(WorldTriggerLineSide::Blue),
        exhaustive->getTotalTriggerCount()};

    require(acceleratedCounts == exhaustiveCounts,
            "accelerated trigger results differ from exhaustive results for line " +
                std::to_string(i));
    counts.push_back(acceleratedCounts);
  }

  return counts;
}

std::vector<TriggerLineSpec> makeLayeredLines(
    wp::Vector2 const& p0,
    wp::Vector2 const& p1) {
  return {
      {1, p0, p1},
      {2, p0, p1},
      {3, p0, p1},
      {BW_LAYER_ALL, p0, p1},
  };
}

LayerSelection selectedLayers() {
  LayerSelection layers;
  layers.set(1);
  layers.set(2);
  return layers;
}

void stationarySweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeLayeredLines({12.0f, 0.0f}, {30.0f, 0.0f}),
      {{9.0f, -10.0f}, {9.0f, -10.0f}}, 4.0f, selectedLayers());

  for (auto const& lineCounts : counts) {
    require(lineCounts[2] == 0,
            "a stationary player update unexpectedly triggered a line");
  }
}

void movingRadiusExpandedSweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeLayeredLines({12.0f, 0.0f}, {30.0f, 0.0f}),
      {{9.0f, -10.0f}, {9.0f, 10.0f}}, 4.0f, selectedLayers());

  require(counts[0][2] == 1 && counts[1][2] == 1,
          "selected trigger lines were missed by the radius-expanded sweep");
  require(counts[2][2] == 0,
          "an unselected-layer trigger line was checked");
  require(counts[3][2] == 1,
          "an all-layer trigger line was missed by the radius-expanded sweep");
}

void longStepSweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeLayeredLines({0.0f, -20.0f}, {0.0f, 20.0f}),
      {{-450.0f, 0.0f}, {450.0f, 0.0f}}, 1.0f, selectedLayers());

  require(counts[0][2] == 1 && counts[1][2] == 1,
          "a long player step missed selected trigger lines");
  require(counts[2][2] == 0,
          "a long player step checked an unselected-layer trigger line");
  require(counts[3][2] == 1,
          "a long player step missed an all-layer trigger line");
}

}  // namespace

int main() {
  try {
    stationarySweepMatchesExhaustive();
    movingRadiusExpandedSweepMatchesExhaustive();
    longStepSweepMatchesExhaustive();
    std::cout << "Accelerated trigger-line sweeps match exhaustive collision checks\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
