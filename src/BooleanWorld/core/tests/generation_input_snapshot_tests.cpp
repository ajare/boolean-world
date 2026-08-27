#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <core/ArrangementWorldDataGenerator.h>
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

void chipParametersAreResolvedInSnapshotOrderOnTheCallingThread() {
  std::unique_ptr<MeshPrimitive> first(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      {rectangle(0.0f, 0.0f, 20.0f, 20.0f)}));
  std::unique_ptr<MeshPrimitive> second(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      {rectangle(5.0f, 5.0f, 15.0f, 15.0f)}));
  auto firstProperties = first->getProperties();
  firstProperties.wallMaterialId = "soft_stone";
  first->setProperties(firstProperties);
  auto secondProperties = second->getProperties();
  secondProperties.floorZ = 10.0f;
  secondProperties.wallMaterialId = "hard_slate";
  second->setProperties(secondProperties);

  std::vector<Primitive*> primitives{first.get(), second.get()};
  std::vector<std::string> consulted;
  auto const callingThread = std::this_thread::get_id();
  auto snapshots = bw::core::SnapshotPrimitives(
      primitives, {}, [&](std::string const& id) {
        require(std::this_thread::get_id() == callingThread,
                "chip resolver left the snapshot calling thread");
        consulted.push_back(id);
        return id == "soft_stone"
                   ? bw::core::ChipGenerationParameters{
                         2.0f, 4.0f, 4.0f, 3.0f, 4.0f, 4.1f, 1.0f, 1.5f, 3.5f, 0.75f, {bw::core::ChipType::PrismaticNotch, bw::core::ChipType::SteppedFracture}}
                   : bw::core::ChipGenerationParameters{1.5f, 1.0f, 1.0f, 2.0f, 3.0f, 3.1f, 1.0f, 0.5f, 2.5f, 0.25f, {bw::core::ChipType::MultiFacetSpall}};
      });

  require(consulted == std::vector<std::string>{"soft_stone", "hard_slate"},
          "chip resolver was not consulted in property-palette order");
  auto arrangement = bw::core::arr::BuildArrangement(snapshots);
  require(arrangement->chipParametersPalette.size() == arrangement->palette.size(),
          "resolved Chip parameters did not stay parallel to the property palette");
  require(arrangement->chipParametersPalette[1].maximumDepth == 4.0f &&
              arrangement->chipParametersPalette[1].maximumReach == 4.0f &&
              arrangement->chipParametersPalette[1].cornerProbability ==
                  0.75f &&
              arrangement->chipParametersPalette[1].types.size() == 2 &&
              arrangement->chipParametersPalette[2].maximumDepth == 1.0f &&
              arrangement->chipParametersPalette[2].maximumReach == 3.0f &&
              arrangement->chipParametersPalette[2].cornerProbability ==
                  0.25f &&
              arrangement->chipParametersPalette[2].types ==
                  std::vector<bw::core::ChipType>{
                      bw::core::ChipType::MultiFacetSpall},
          "resolved Chip parameters landed in the wrong palette order");

  auto unresolved = bw::core::SnapshotPrimitives(primitives);
  for (auto const& primitive : unresolved) {
    require(
        primitive.chipParameters.probability == 0.0f &&
            primitive.chipParameters.cornerProbability == 0.0f,
        "an absent Chip resolver did not snapshot disabled generation");
  }
  bw::core::ArrangementWorldData withoutResolver(
      bw::core::arr::BuildArrangement(unresolved),
      wp::BoundingBox({-10.0f, -10.0f}, {40.0f, 40.0f}), 8.0f, 2.0f);
  require(withoutResolver.getDetail().getChipCount() == 0,
          "an absent chip resolver still produced Chips");
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
    chipParametersAreResolvedInSnapshotOrderOnTheCallingThread();
    primitiveRemovalBeforeCompletionAndCommitIsSafe();
    std::cout << "Generation workers and metadata use lifetime-safe snapshots\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
