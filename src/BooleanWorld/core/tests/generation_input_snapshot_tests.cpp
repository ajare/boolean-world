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
      Primitive::FillRule::NonZero,
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

}  // namespace

int main() {
  try {
    generationWorkerUsesCapturedPrimitiveSnapshot();
    std::cout << "Generation workers use captured primitive snapshots\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
