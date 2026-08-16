#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>

#include <core/DynamicWorldDataGenerator.h>
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

void addPrimitive(bw::core::World& world) {
  world.addPrimitive(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      {rectangle(0.0f, 0.0f, 10.0f, 10.0f)}));
}

void committedCallbackCanReadGeneratorState() {
  bw::core::World world(20.0f, 2.0f);
  addPrimitive(world);
  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);

  bool committed = false;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Committed) {
          auto activePrimitives = generator.getActiveClippingPrimitives();
          require(
              activePrimitives.size() == 1,
              "committed callback could not read the active clipping");
          committed = true;
        }
      });

  generator.generateBlocking();
  generator.getWorldData(&world);

  require(committed, "generation was not committed");
  generator.unregisterGenerationCallback(token);
}

void unregisterWaitsForRunningCallbackAndPreventsFutureCallbacks() {
  bw::core::World world(20.0f, 2.0f);
  addPrimitive(world);
  DynamicWorldDataGenerator generator(&world);

  std::mutex mutex;
  std::condition_variable changed;
  bool callbackRunning = false;
  bool callbackMayFinish = false;
  uint32_t callbackCount = 0;

  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        if (details.state !=
            DynamicWorldDataGenerator::GenerationState::Generating) {
          return;
        }

        std::unique_lock lock(mutex);
        callbackRunning = true;
        changed.notify_all();
        changed.wait(lock, [&] { return callbackMayFinish; });
        callbackCount++;
      });

  generator.generate(&world, true);
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return callbackRunning; }),
        "generation callback did not start");
  }

  auto unregistration = std::async(std::launch::async, [&] {
    generator.unregisterGenerationCallback(token);
  });
  require(
      unregistration.wait_for(100ms) == std::future_status::timeout,
      "unregistration returned while its callback was running");

  {
    std::lock_guard lock(mutex);
    callbackMayFinish = true;
  }
  changed.notify_all();

  require(
      unregistration.wait_for(10s) == std::future_status::ready,
      "unregistration did not complete after its callback finished");
  unregistration.get();

  generator.generateBlocking();
  {
    std::lock_guard lock(mutex);
    require(callbackCount == 1, "an unregistered callback was invoked");
  }
}

}  // namespace

int main() {
  try {
    committedCallbackCanReadGeneratorState();
    unregisterWaitsForRunningCallbackAndPreventsFutureCallbacks();
    std::cout << "Generation callbacks have safe locking and lifetime semantics\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
