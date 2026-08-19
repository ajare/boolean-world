#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <core/DynamicWorldDataGenerator.h>
#include <core/Layer.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

namespace {

using bw::core::ComplexPolygon;
using bw::core::DynamicWorldDataGenerator;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using namespace std::chrono_literals;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ComplexPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

void addLayerPrimitive(
    bw::core::Layer& layer,
    ComplexPolygon polygon) {
  auto primitive = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      {std::move(polygon)});
  layer.addPrimitive(primitive);
}

void blockedWorkerCoalescesToLatestGenerationSnapshot() {
  bw::core::World world(100.0f, 10.0f);
  auto* firstLayer = world.getActiveLayer();
  auto* secondLayer = world.addLayer("second");
  addLayerPrimitive(*firstLayer, rectangle(0.0f, 0.0f, 10.0f, 10.0f));
  addLayerPrimitive(*secondLayer, rectangle(20.0f, 20.0f, 30.0f, 30.0f));

  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);

  std::mutex mutex;
  std::condition_variable changed;
  bool firstWorkerStarted = false;
  bool firstWorkerMayContinue = false;
  std::vector<uint32_t> generatedIds;
  uint64_t reportedCoalescedRequests = 0;

  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        std::unique_lock lock(mutex);
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generating) {
          if (!firstWorkerStarted) {
            firstWorkerStarted = true;
            changed.notify_all();
            changed.wait(lock, [&] { return firstWorkerMayContinue; });
          }
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          generatedIds.push_back(details.clippingId);
          reportedCoalescedRequests =
              details.stats.generationRequests.coalescedRequestCount;
          changed.notify_all();
        }
      });

  generator.generate(&world, true);
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return firstWorkerStarted; }),
        "generation worker did not reach the blocking callback");
  }
  require(
      generator.getNumGenerationsInProgress() == 1,
      "blocked worker was not counted as in progress");

  constexpr uint32_t requestCount = 3000;
  for (uint32_t i = 0; i < requestCount; ++i) {
    generator.setActiveLayer(
        i % 2 == 0 ? firstLayer->getId() : secondLayer->getId());
    generator.generate(&world, true);
    require(
        generator.getNumGenerationsPending() <= 1,
        "generation request queue exceeded its one-snapshot bound");
    require(
        generator.getNumGenerationsInProgress() <= 1,
        "more than one asynchronous generation ran concurrently");
  }

  require(
      generator.getNumGenerationsPending() == 1,
      "latest generation snapshot was not retained");
  require(
      generator.getNumGenerationRequestsCoalesced() == requestCount - 1,
      "superseded generation requests were not counted");

  {
    std::lock_guard lock(mutex);
    firstWorkerMayContinue = true;
  }
  changed.notify_all();

  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return generatedIds.size() == 2; }),
        "latest pending generation did not eventually complete");
    require(
        generatedIds[1] == generatedIds[0] + 1,
        "coalescing changed generation identity ordering");
    require(
        reportedCoalescedRequests == requestCount - 1,
        "generation statistics did not expose coalesced requests");
  }

  require(
      generator.getNumGenerationsPending() == 0,
      "pending generation snapshot was not consumed");
  require(
      generator.getNumGenerationsInProgress() == 0,
      "generation remained in progress after completion");
  require(
      generator.getNumGenerationsComplete() == 2,
      "superseded requests performed generation work");

  // The final request selected the second Layer, so only its rectangle may
  // appear: coalescing must keep the latest selection, not an earlier one.
  auto worldData = generator.getWorldData(&world);
  require(
      worldData->getContainingFaceIndex({25.0f, 25.0f}) != ~0u,
      "latest request lost its layer selection");
  require(
      worldData->getContainingFaceIndex({5.0f, 5.0f}) == ~0u,
      "an older request's layer selection was generated");

  generator.unregisterGenerationCallback(token);
}

template <typename Callback>
void requireInvalidInterval(Callback&& callback, char const* message) {
  try {
    callback();
  } catch (std::invalid_argument const&) {
    return;
  }
  throw std::runtime_error(message);
}

void invalidScheduleIntervalsAreRejected() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);

  requireInvalidInterval(
      [&] { generator.setScheduledGenerationInterval(0.0f); },
      "zero schedule interval was accepted");
  requireInvalidInterval(
      [&] { generator.setScheduledGenerationInterval(-1.0f); },
      "negative schedule interval was accepted");
  requireInvalidInterval(
      [&] {
        generator.setScheduledGenerationInterval(
            std::numeric_limits<float>::infinity());
      },
      "infinite schedule interval was accepted");
  requireInvalidInterval(
      [&] {
        generator.startGenerationSchedule(
            std::numeric_limits<float>::quiet_NaN());
      },
      "NaN schedule interval was accepted");
  require(
      !generator.isScheduledGenerationRunning(),
      "invalid interval started the generation scheduler");
  require(
      generator.getScheduledGenerationInterval() == 5.0f,
      "rejected interval changed the configured schedule");
}

}  // namespace

int main() {
  try {
    blockedWorkerCoalescesToLatestGenerationSnapshot();
    invalidScheduleIntervalsAreRejected();
    std::cout << "Asynchronous generation work is bounded and coalesced\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
