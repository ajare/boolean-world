#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
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

void invalidGenerationStartIntervalsAreRejected() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);

  generator.setGenerationStartInterval(0.0f);
  require(
      generator.getGenerationStartInterval() == 0.0f,
      "zero generation start interval was not retained");
  requireInvalidInterval(
      [&] { generator.setGenerationStartInterval(-1.0f); },
      "negative generation start interval was accepted");
  requireInvalidInterval(
      [&] {
        generator.setGenerationStartInterval(
            std::numeric_limits<float>::infinity());
      },
      "infinite generation start interval was accepted");
  requireInvalidInterval(
      [&] {
        generator.startGenerationSchedule(
            std::numeric_limits<float>::quiet_NaN());
      },
      "NaN generation start interval was accepted");
  require(
      !generator.isScheduledGenerationRunning(),
      "invalid interval started the generation scheduler");
  require(
      generator.getGenerationStartInterval() == 0.0f,
      "rejected interval changed the configured generation start interval");
}

struct GenerationObserver {
  std::mutex mutex;
  std::condition_variable changed;
  uint32_t starts{0};
  uint32_t completions{0};

  void observe(DynamicWorldDataGenerator::GenerationDetails const& details) {
    std::lock_guard lock(mutex);
    if (details.state ==
        DynamicWorldDataGenerator::GenerationState::Generating) {
      ++starts;
    } else if (
        details.state ==
        DynamicWorldDataGenerator::GenerationState::Generated) {
      ++completions;
    }
    changed.notify_all();
  }

  void waitForCompletions(uint32_t expected, char const* message) {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return completions >= expected; }),
        message);
  }

  uint32_t startCount() {
    std::lock_guard lock(mutex);
    return starts;
  }
};

void updateGenerator(DynamicWorldDataGenerator& generator, float frameTime) {
  generator.update(frameTime, {}, 0);
}

void waitForStartOnAnIdleUpdate(
    DynamicWorldDataGenerator& generator,
    GenerationObserver& observer,
    uint32_t expected,
    char const* message) {
  for (int attempt = 0; attempt < 10000; ++attempt) {
    updateGenerator(generator, 0.0f);
    if (observer.startCount() >= expected) {
      return;
    }
    std::this_thread::yield();
  }
  throw std::runtime_error(message);
}

void positiveIntervalUsesBootstrapAsStartAnchor() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  GenerationObserver observer;
  auto token = generator.registerGenerationCallback(
      [&](auto const& details) { observer.observe(details); });

  generator.getWorldData(&world);
  require(observer.startCount() == 1, "bootstrap Generation was not observed");
  generator.startGenerationSchedule(2.0f);
  require(
      observer.startCount() == 1,
      "positive schedule duplicated the bootstrap Generation");

  updateGenerator(generator, 1.5f);
  require(observer.startCount() == 1, "Generation started before its deadline");
  updateGenerator(generator, 0.5f);
  observer.waitForCompletions(2, "eligible positive-interval Generation did not complete");
  require(observer.startCount() == 2, "positive interval started a backlog");

  generator.stopGenerationSchedule();
  generator.unregisterGenerationCallback(token);
}

void overrunDoesNotAccumulateScheduledBacklog() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  generator.getWorldData(&world);

  std::mutex mutex;
  std::condition_variable changed;
  bool scheduledStarted = false;
  bool mayComplete = false;
  uint32_t starts = 0;
  uint32_t completions = 0;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        std::unique_lock lock(mutex);
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generating) {
          ++starts;
          if (starts == 1) {
            scheduledStarted = true;
            changed.notify_all();
            changed.wait(lock, [&] { return mayComplete; });
          }
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          ++completions;
          changed.notify_all();
        }
      });

  generator.startGenerationSchedule(1.0f);
  updateGenerator(generator, 1.0f);
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return scheduledStarted; }),
        "scheduled Generation did not start");
  }

  updateGenerator(generator, 20.0f);
  require(
      generator.getNumGenerationsPending() == 0,
      "slow Generation accumulated scheduled work");
  {
    std::lock_guard lock(mutex);
    mayComplete = true;
  }
  changed.notify_all();
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return completions == 1; }),
        "overrunning Generation did not complete");
  }

  for (int attempt = 0; attempt < 10000; ++attempt) {
    updateGenerator(generator, 0.0f);
    {
      std::lock_guard lock(mutex);
      if (starts == 2) {
        break;
      }
    }
    std::this_thread::yield();
  }
  {
    std::lock_guard lock(mutex);
    require(starts == 2, "overrun was not followed on the first idle update");
  }
  require(
      generator.getNumGenerationsPending() == 0,
      "overrun replayed more than one missed interval");

  generator.stopGenerationSchedule();
  // The second worker is no longer blocked; wait for destruction safety.
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return completions == 2; }),
        "post-overrun Generation did not complete");
  }
  generator.unregisterGenerationCallback(token);
}

void zeroIntervalRestartsOnNextMainThreadUpdate() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  GenerationObserver observer;
  auto token = generator.registerGenerationCallback(
      [&](auto const& details) { observer.observe(details); });

  generator.getWorldData(&world);
  generator.startGenerationSchedule(0.0f);
  require(observer.startCount() == 1, "zero interval duplicated bootstrap immediately");
  waitForStartOnAnIdleUpdate(
      generator, observer, 2,
      "zero interval did not restart after bootstrap");
  observer.waitForCompletions(2, "zero-interval restart did not complete");
  require(observer.startCount() == 2, "zero interval started more than one worker");
  waitForStartOnAnIdleUpdate(
      generator, observer, 3,
      "zero interval did not restart completed work");
  observer.waitForCompletions(3, "second zero-interval restart did not complete");

  generator.stopGenerationSchedule();
  generator.unregisterGenerationCallback(token);
}

void adHocGenerationResetsTheIntervalAnchor() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  GenerationObserver observer;
  auto token = generator.registerGenerationCallback(
      [&](auto const& details) { observer.observe(details); });

  generator.getWorldData(&world);
  generator.startGenerationSchedule(3.0f);
  updateGenerator(generator, 2.0f);
  generator.generate();
  observer.waitForCompletions(2, "ad-hoc Generation did not complete immediately");

  updateGenerator(generator, 2.0f);
  require(
      observer.startCount() == 2,
      "periodic deadline was not reset by the ad-hoc start");
  updateGenerator(generator, 1.0f);
  waitForStartOnAnIdleUpdate(
      generator, observer, 3,
      "periodic work did not follow the ad-hoc anchor");
  observer.waitForCompletions(3, "post-ad-hoc periodic work did not complete");

  generator.stopGenerationSchedule();
  generator.unregisterGenerationCallback(token);
}

void liveIntervalChangesRecalculateFromLatestStart() {
  bw::core::World world(20.0f, 2.0f);
  DynamicWorldDataGenerator generator(&world);
  GenerationObserver observer;
  auto token = generator.registerGenerationCallback(
      [&](auto const& details) { observer.observe(details); });

  generator.getWorldData(&world);
  generator.startGenerationSchedule(10.0f);
  updateGenerator(generator, 4.0f);
  require(observer.startCount() == 1, "long interval started too early");
  generator.setGenerationStartInterval(3.0f);
  updateGenerator(generator, 0.0f);
  observer.waitForCompletions(2, "shortened live interval did not apply immediately");

  generator.setGenerationStartInterval(10.0f);
  updateGenerator(generator, 5.0f);
  require(observer.startCount() == 2, "lengthened live interval used the old deadline");
  generator.setGenerationStartInterval(1.0f);
  waitForStartOnAnIdleUpdate(
      generator, observer, 3,
      "second shortened interval did not use latest start");
  observer.waitForCompletions(3, "second shortened-interval work did not complete");

  generator.stopGenerationSchedule();
  generator.unregisterGenerationCallback(token);
}

}  // namespace

int main() {
  try {
    blockedWorkerCoalescesToLatestGenerationSnapshot();
    invalidGenerationStartIntervalsAreRejected();
    positiveIntervalUsesBootstrapAsStartAnchor();
    overrunDoesNotAccumulateScheduledBacklog();
    zeroIntervalRestartsOnNextMainThreadUpdate();
    adHocGenerationResetsTheIntervalAnchor();
    liveIntervalChangesRecalculateFromLatestStart();
    std::cout << "Asynchronous generation cadence is bounded and controllable\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
