#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <core/DynamicWorldDataGenerator.h>
#include <core/World.h>

namespace {

using bw::core::DynamicWorldDataGenerator;
using namespace std::chrono_literals;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void staleLayerGenerationDoesNotBlockCurrentGeneration() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);

  std::mutex mutex;
  std::condition_variable changed;
  std::vector<uint32_t> generatedIds;
  std::vector<uint32_t> committedIds;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        std::lock_guard lock(mutex);
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          generatedIds.push_back(details.clippingId);
          changed.notify_all();
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Committed) {
          committedIds.push_back(details.clippingId);
        }
      });

  generator.generateBlocking();
  generator.setActiveLayer(1);

  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return generatedIds.size() == 2; }),
        "current-layer generation did not complete");
  }

  generator.getWorldData(&world);

  {
    std::lock_guard lock(mutex);
    require(committedIds.size() == 1, "current generation was not committed");
    require(
        committedIds.front() == generatedIds.back(),
        "stale generation blocked the current generation or changed its id");
  }

  generator.unregisterGenerationCallback(token);
}

void generatedArtifactsFinishBeforeCommitBecomesVisible() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);
  generator.generateBlocking();
  auto original = generator.getWorldData(&world);

  std::mutex mutex;
  std::condition_variable changed;
  bool preparing = false;
  bool releasePreparation = false;
  bw::core::WorldDataPtr generatedWorld;
  bw::core::WorldDataPtr committedWorld;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          std::unique_lock lock(mutex);
          generatedWorld = details.worldData;
          preparing = true;
          changed.notify_all();
          changed.wait(lock, [&] { return releasePreparation; });
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Committed) {
          std::lock_guard lock(mutex);
          committedWorld = details.worldData;
        }
      });

  generator.generate(&world);
  {
    std::unique_lock lock(mutex);
    require(changed.wait_for(lock, 10s, [&] { return preparing; }),
            "generated callback did not begin artifact preparation");
  }
  require(generatedWorld != nullptr,
          "generated callback did not receive its immutable World snapshot");
  require(generator.getWorldData(&world) == original,
          "World committed before generated artifact preparation completed");

  {
    std::lock_guard lock(mutex);
    releasePreparation = true;
  }
  changed.notify_all();

  auto deadline = std::chrono::steady_clock::now() + 10s;
  bw::core::WorldDataPtr current;
  do {
    current = generator.getWorldData(&world);
    if (current == generatedWorld) break;
    std::this_thread::sleep_for(1ms);
  } while (std::chrono::steady_clock::now() < deadline);

  require(current == generatedWorld && committedWorld == generatedWorld,
          "commit did not publish the callback-prepared World snapshot");
  generator.unregisterGenerationCallback(token);
}

void pendingGenerationQueueDropsOldestEntriesAtItsBound() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);

  std::vector<uint32_t> generatedIds;
  std::vector<uint32_t> committedIds;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          generatedIds.push_back(details.clippingId);
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Committed) {
          committedIds.push_back(details.clippingId);
        }
      });

  auto constexpr generationCount =
      DynamicWorldDataGenerator::MaxPendingGenerations + 2;
  for (std::size_t i = 0; i < generationCount; ++i) {
    generator.generateBlocking();
  }
  generator.getWorldData(&world);

  require(
      generatedIds.size() == generationCount,
      "test did not produce every pending generation");
  require(committedIds.size() == 1, "pending generation was not committed");
  require(
      committedIds.front() ==
          generatedIds[generationCount -
                       DynamicWorldDataGenerator::MaxPendingGenerations],
      "commit queue retained an entry older than its bound");

  generator.unregisterGenerationCallback(token);
}

}  // namespace

int main() {
  try {
    staleLayerGenerationDoesNotBlockCurrentGeneration();
    generatedArtifactsFinishBeforeCommitBecomesVisible();
    pendingGenerationQueueDropsOldestEntriesAtItsBound();
    std::cout << "Commit queue discards stale and excess generations\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
