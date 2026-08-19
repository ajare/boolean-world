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

std::vector<TriggerCounts> runAcceleratedAndExhaustive(
    std::vector<TriggerLineSpec> const& specs,
    std::vector<wp::Vector2> const& positions,
    float radius,
    LayerSelection const& layerSelection) {
  World world(1000.0f, 50.0f);
  std::vector<std::unique_ptr<WorldTriggerLine>> exhaustiveLines;
  exhaustiveLines.reserve(specs.size());

  for (auto const& spec : specs) {
    world.addTriggerLine(new WorldTriggerLine(spec.p0, spec.p1, spec.side));
    exhaustiveLines.push_back(
        std::make_unique<WorldTriggerLine>(spec.p0, spec.p1, spec.side));
  }

  for (size_t i = 0; i < positions.size(); ++i) {
    world.update(0.0f, updateData(positions[i], radius, layerSelection),
                 {1000.0f, 1000.0f});

    if (i == 0) {
      continue;
    }

    for (auto const& triggerLine : exhaustiveLines) {
      triggerLine->checkCollide(positions[i - 1], positions[i], radius);
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

// Several coincident lines, so the sweep has to report every line sharing a
// grid cell rather than just the first one it finds.
std::vector<TriggerLineSpec> makeCoincidentLines(
    wp::Vector2 const& p0,
    wp::Vector2 const& p1) {
  return {
      {p0, p1},
      {p0, p1},
      {p0, p1},
      {p0, p1},
  };
}

void stationarySweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeCoincidentLines({12.0f, 0.0f}, {30.0f, 0.0f}),
      {{9.0f, -10.0f}, {9.0f, -10.0f}}, 4.0f, bw::core::SelectLayer(0));

  for (auto const& lineCounts : counts) {
    require(lineCounts[2] == 0,
            "a stationary player update unexpectedly triggered a line");
  }
}

void movingRadiusExpandedSweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeCoincidentLines({12.0f, 0.0f}, {30.0f, 0.0f}),
      {{9.0f, -10.0f}, {9.0f, 10.0f}}, 4.0f, bw::core::SelectLayer(0));

  for (auto const& lineCounts : counts) {
    require(lineCounts[2] == 1,
            "a trigger line was missed by the radius-expanded sweep");
  }
}

void longStepSweepMatchesExhaustive() {
  auto const counts = runAcceleratedAndExhaustive(
      makeCoincidentLines({0.0f, -20.0f}, {0.0f, 20.0f}),
      {{-450.0f, 0.0f}, {450.0f, 0.0f}}, 1.0f, bw::core::SelectLayer(0));

  for (auto const& lineCounts : counts) {
    require(lineCounts[2] == 1,
            "a long player step missed a trigger line");
  }
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
