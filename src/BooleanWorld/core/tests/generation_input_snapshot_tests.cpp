#include <chrono>
#include <condition_variable>
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

void generationWorkerUsesCapturedPrimitiveSnapshot() {
  bw::core::World world(20.0f, 2.0f);
  auto primitive = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      {rectangle(0.0f, 0.0f, 10.0f, 10.0f)});
  auto properties = primitive->getProperties();
  properties.floorZ = 3.0f;
  primitive->setProperties(properties);
  world.addPrimitive(primitive);

  DynamicWorldDataGenerator generator(&world);
  generator.setAllowCommitIfVisible(true);

  std::mutex mutex;
  std::condition_variable changed;
  bool workerStarted = false;
  bool workerMayContinue = false;
  bool generationComplete = false;

  generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        std::unique_lock lock(mutex);
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generating) {
          workerStarted = true;
          changed.notify_all();
          changed.wait(lock, [&] { return workerMayContinue; });
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          generationComplete = true;
          changed.notify_all();
        }
      });

  generator.generate(&world, true);

  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return workerStarted; }),
        "generation worker did not start");
  }

  // These edits happen after the asynchronous generation was posted but before
  // its arrangement work begins. The generation must retain the old operation,
  // contours, and property palette.
  primitive->setOperation(Primitive::Operation::Difference);
  primitive->setSize(2.0f, 2.0f);
  primitive->updateVertexPositions();
  properties.floorZ = 99.0f;
  primitive->setProperties(properties);

  {
    std::lock_guard lock(mutex);
    workerMayContinue = true;
  }
  changed.notify_all();

  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return generationComplete; }),
        "generation worker did not complete");
  }

  auto worldData = generator.getWorldData(&world);
  auto faceIndex = worldData->getContainingFaceIndex({1.0f, 1.0f});
  require(
      faceIndex != ~0u,
      "worker observed primitive geometry or operation changed after dispatch");

  auto const& arrangement = worldData->getArrangement();
  auto const& face = arrangement.faces[faceIndex];
  require(
      arrangement.palette[face.paletteIndex].floorZ == 3.0f,
      "worker observed primitive properties changed after dispatch");
}

void primitiveRemovalBeforeCompletionAndCommitIsSafe() {
  bw::core::World world(100.0f, 10.0f);
  auto primitive = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      {rectangle(-10.0f, -10.0f, 10.0f, 10.0f)});
  {
    auto mutation = primitive->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale)
        .setPoints({{0.0f, 1.0f}, {1.0f, 2.0f}});
  }
  world.addPrimitive(primitive);

  DynamicWorldDataGenerator generator(&world);
  generator.setAlwaysUpdateVertices(true);
  bw::core::WorldUpdateData viewData{
      {0.0f, 0.0f},
      0.0f,
      1.0f,
      90.0f,
      100.0f,
      false,
      false,
      bw::core::SelectLayer(0)};
  generator.update(0.0f, viewData, 0);

  // Establish an active generation so the next one uses the ordinary
  // same-layer visibility gate rather than the layer-change bypass.
  generator.generateBlocking();
  generator.getWorldData(&world);
  require(generator.getNumCommits() == 1, "baseline generation did not commit");

  std::mutex mutex;
  std::condition_variable changed;
  bool workerStarted = false;
  bool workerMayContinue = false;
  bool generationComplete = false;
  auto token = generator.registerGenerationCallback(
      [&](DynamicWorldDataGenerator::GenerationDetails const& details) {
        std::unique_lock lock(mutex);
        if (details.state ==
            DynamicWorldDataGenerator::GenerationState::Generating) {
          workerStarted = true;
          changed.notify_all();
          changed.wait(lock, [&] { return workerMayContinue; });
        } else if (
            details.state ==
            DynamicWorldDataGenerator::GenerationState::Generated) {
          generationComplete = true;
          changed.notify_all();
        }
      });

  generator.generate(&world, true);
  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return workerStarted; }),
        "generation worker did not start before primitive removal");
  }

  auto const sourcePrimitives = generator.getSourceClippingPrimitives();
  auto const updatedPrimitives =
      generator.getSourceClippingUpdatedPrimitives();
  require(
      sourcePrimitives.size() == 1 && sourcePrimitives.front().id == 0,
      "source generation diagnostics lost the primitive identity");
  require(
      updatedPrimitives.size() == 1 && updatedPrimitives.front().id == 0,
      "updated generation diagnostics lost the primitive identity");

  // World owns and destroys the primitive here. The worker and pending
  // generation must need only the value snapshots captured above.
  world.removePrimitive(primitive);
  require(world.getNumPrimitives() == 0, "test primitive was not removed");

  {
    std::lock_guard lock(mutex);
    workerMayContinue = true;
  }
  changed.notify_all();

  {
    std::unique_lock lock(mutex);
    require(
        changed.wait_for(lock, 10s, [&] { return generationComplete; }),
        "generation worker did not complete after primitive removal");
  }

  generator.getWorldData(&world);
  require(
      generator.getNumCommits() == 1,
      "visible generated geometry bypassed the commit gate after removal");

  generator.setAllowCommitIfVisible(true);
  auto worldData = generator.getWorldData(&world);
  require(
      generator.getNumCommits() == 2,
      "generation could not commit after its primitive was destroyed");
  require(
      worldData->getContainingFaceIndex({1.0f, 1.0f}) != ~0u,
      "committed generation lost its captured primitive geometry");

  auto const activePrimitives = generator.getActiveClippingPrimitives();
  auto const activeUpdatedPrimitives =
      generator.getActiveClippingUpdatedPrimitives();
  require(
      activePrimitives.size() == 1 && activePrimitives.front().id == 0,
      "active generation diagnostics lost the removed source primitive");
  require(
      activeUpdatedPrimitives.size() == 1 &&
          activeUpdatedPrimitives.front().id == 0,
      "active generation diagnostics lost the removed updated primitive");

  generator.unregisterGenerationCallback(token);
}

}  // namespace

int main() {
  try {
    generationWorkerUsesCapturedPrimitiveSnapshot();
    primitiveRemovalBeforeCompletionAndCommitIsSafe();
    std::cout << "Generation workers and metadata use lifetime-safe snapshots\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
