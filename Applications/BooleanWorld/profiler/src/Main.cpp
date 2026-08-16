#include <iostream>
#include <filesystem>

#include <Superluminal/PerformanceAPI.h>

#include <willpower/common/Timer.h>

#include <core/YamlSerializer.h>
#include <core/World.h>
#include <core/DynamicWorldDataGenerator.h>

using namespace std;

shared_ptr<bw::core::World> createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto genFn = [world](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
    auto wdg = new bw::core::DynamicWorldDataGenerator(world.get());

    wdg->setAlwaysUpdateVertices(true);
    wdg->setAllowCommitIfVisible(true);

    return wdg;
  };

  world->setWorldDataGeneratorFactory(genFn);

  return world;
}

shared_ptr<bw::core::World> openWorld(string const& filepath) {
  auto path = filesystem::path(filepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  shared_ptr<bw::core::World> world;

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      cout << e.what() << "\n";
      return nullptr;
    }

    world = createWorld(8192, 8192);

    auto workData = bw::core::SerializationWorkData{};

    if (world->deserialize(ser, workData)) {
      auto const& warnings = world->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          cout << warning << "\n";
        }
      }

      return world;
    } else {
      auto const& errors = world->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          cout << error << "\n";
        }
      }

      return nullptr;
    }
  } else {
    cout << "Unsupported file format.\n";
    return nullptr;
  }
}

int main(int argc, char** argv) {
  string filename;
  if (argc < 2) {
    filename = "../../../../app/resources/stress-test-1.yaml";
  } else {
    filename = argv[1];
  }

  auto world = openWorld(filename);
  auto dataGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(world->getWorldDataGenerator());

  Clipper2Lib::WmInitialiseAllocators(4, 16 * 1024 * 1024);

  // Run a few times to "warm up"
  for (int i = 0; i < 5; ++i) {
    dataGenerator->generateBlocking();
  }

  PerformanceAPI_SetCurrentThreadName("Main");

  for (int i = 0; i < 10; ++i) {
    wp::Timer timer;

    auto eventName = format("GenClip {}", i);
    PerformanceAPI_BeginEvent(eventName.c_str(), nullptr, PERFORMANCEAPI_DEFAULT_COLOR);
    dataGenerator->generateBlocking();
    PerformanceAPI_EndEvent();

    auto totalTime = timer.elapsedNanoseconds();

    auto worldData = dataGenerator->getWorldData(world.get());
    auto const& stats = worldData->getStats();

    /*
    cout << "Primitives: candidate count: " << stats.prim.candidateCount << "\n";
    cout << "Primitives: visible count: " << stats.prim.visibleCount << "\n";
    cout << "Primitives: processed " << stats.clip.primitivesProcessed << "\n";
    cout << "Primitives: vertices processed: " << stats.clip.primVerticesProcessed << "\n";
    cout << "Polygons: generated: " << stats.clip.polygonsGenerated << "\n";
    cout << "Polygons: vertices generated: " << stats.clip.verticesGenerated << "\n";
    cout << "Triangulation: triangles generated: " << stats.tri.trianglesGenerated << "\n";
    cout << "Generation time " << dataGenerator->getLastGenTime() / 1'000'000.0 << " ms\n";
    */

    cout << "Profile time " << totalTime / 1'000'000.0 << " ms\n";
  }

  Clipper2Lib::WmDestroyAllocators();

  return 0;
}