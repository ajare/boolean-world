#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <core/LayerSelection.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/WorldUpdateData.h>

namespace {

using bw::core::LayerSelection;
using bw::core::World;
using bw::core::WorldTriggerLine;
using bw::core::WorldUpdateData;

wp::BoundingBox sweptBounds(
    wp::Vector2 const& oldPosition,
    wp::Vector2 const& newPosition,
    float radius) {
  auto bounds = wp::BoundingBox(oldPosition, newPosition - oldPosition);
  bounds.inflate(radius);
  return bounds;
}

WorldUpdateData updateData(wp::Vector2 const& position, float radius) {
  return {position, 0.0f, radius, 0.0f, 0.0f, false, false,
          bw::core::SelectAllLayers()};
}

}  // namespace

int main() {
  constexpr uint32_t dimension = 128;
  constexpr uint32_t triggerLineCount = dimension * dimension;
  constexpr uint32_t frameCount = 256;
  constexpr float worldSize = 8192.0f;
  constexpr float spacing = 60.0f;
  constexpr float radius = 4.0f;

  World world(worldSize, 64.0f);
  std::vector<std::unique_ptr<WorldTriggerLine>> exhaustiveLines;
  exhaustiveLines.reserve(triggerLineCount);

  for (uint32_t y = 0; y < dimension; ++y) {
    for (uint32_t x = 0; x < dimension; ++x) {
      auto const position = wp::Vector2{
          -3800.0f + float(x) * spacing, -3800.0f + float(y) * spacing};
      world.addTriggerLine(new WorldTriggerLine(
          position + wp::Vector2{-8.0f, 0.0f},
          position + wp::Vector2{8.0f, 0.0f}));
      exhaustiveLines.push_back(std::make_unique<WorldTriggerLine>(
          position + wp::Vector2{-8.0f, 0.0f},
          position + wp::Vector2{8.0f, 0.0f}));
    }
  }

  std::vector<wp::Vector2> positions;
  positions.reserve(frameCount + 1);
  for (uint32_t frame = 0; frame <= frameCount; ++frame) {
    positions.push_back({-120.0f + float(frame), 7.0f});
  }

  uint64_t acceleratedChecks = 0;
  for (uint32_t frame = 1; frame <= frameCount; ++frame) {
    acceleratedChecks += world.findTriggerLines(
                                  sweptBounds(positions[frame - 1], positions[frame], radius))
                             .size();
  }

  auto const acceleratedStart = std::chrono::steady_clock::now();
  for (auto const& position : positions) {
    world.update(0.0f, updateData(position, radius), {worldSize, worldSize});
  }
  auto const acceleratedMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - acceleratedStart)
                                 .count();

  auto const exhaustiveStart = std::chrono::steady_clock::now();
  for (uint32_t frame = 1; frame <= frameCount; ++frame) {
    for (auto const& triggerLine : exhaustiveLines) {
      triggerLine->checkCollide(positions[frame - 1], positions[frame], radius);
    }
  }
  auto const exhaustiveMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - exhaustiveStart)
                                .count();

  auto const exhaustiveChecks = uint64_t(frameCount) * triggerLineCount;
  if (acceleratedChecks >= exhaustiveChecks) {
    std::cerr << "Trigger-line grid did not reduce per-frame collision checks\n";
    return 1;
  }

  std::cout << "Trigger-line local-query benchmark: " << triggerLineCount
            << " sparse trigger lines, " << frameCount << " player updates\n"
            << "  exact collision checks per frame: "
            << double(acceleratedChecks) / frameCount << " grid candidates / "
            << triggerLineCount << " exhaustive\n"
            << "  total exact collision checks: " << acceleratedChecks << " grid candidates / "
            << exhaustiveChecks << " exhaustive\n"
            << "  player updates with grid candidates: " << acceleratedMs << " ms\n"
            << "  exhaustive exact collision checks: " << exhaustiveMs << " ms\n";
}
