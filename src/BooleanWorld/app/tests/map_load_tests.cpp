#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#define class struct
#include <willpower/application/resourcesystem/TextFileResource.h>
#undef class

#include <core/BinarySerializer.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/LayerBuildStep.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/YamlSerializer.h>
#include <core-lua/CoreLua.h>
#include <core-lua/RunScript.h>

#include "Map.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readFixture(std::string const& filename) {
  auto path = std::filesystem::path(BW_MAP_TEST_RESOURCE_DIR) / filename;
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::shared_ptr<wp::application::resourcesystem::TextFileResource> makeWorldResource(
    std::string const& text, std::string const& source = "test.world.yaml") {
  auto resource = std::make_shared<wp::application::resourcesystem::TextFileResource>(
      "world", "", source, std::map<std::string, std::string>{}, nullptr);
  resource->mText = text;
  return resource;
}

void playMapsUseDynamicWorldDataGenerators() {
  bw::core::World defaultWorld;
  require(dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
              defaultWorld.getWorldDataGenerator()) == nullptr,
          "Default worlds unexpectedly satisfy the play-state generator requirement");

  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);
  map.loadWorldFromYaml(makeWorldResource(readFixture("world-test-1.world.yaml")));

  auto* generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      map.getWorld()->getWorldDataGenerator());
  require(generator != nullptr,
          "Loaded play map did not install a dynamic world data generator");
  require(!generator->getCreateWayfinderMesh(),
          "Loaded play map enabled Wayfinder mesh generation by default");
}

void establishedWorldEnablesAndRoundTripsWedges() {
  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);
  map.loadWorldFromYaml(makeWorldResource(readFixture("world-test-1.world.yaml")));
  auto expected = bw::core::WedgeGenerationParameters{};
  expected.enabled = true;
  require(map.getWorld()->getWedgeGenerationParameters() == expected,
          "established test World did not enable the default Wedge ranges");

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  map.getWorld()->serialize(writer, workData);
  auto const& yaml = writer->getSerializedString();
  require(yaml.find("WedgeFacet") == std::string::npos &&
              yaml.find("detailGeometry") == std::string::npos &&
              yaml.find("wedgeCount") == std::string::npos,
          "generated Wedge detail leaked into serialized World data");
  Map roundTripped("map", "", "", {}, nullptr, &logger);
  roundTripped.loadWorldFromYaml(
      makeWorldResource(yaml));
  require(roundTripped.getWorld()->getWedgeGenerationParameters() == expected,
          "established test World's Wedge settings did not round-trip");
}

void failedLoadRetainsThePreviousWorld() {
  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);
  auto resource = makeWorldResource(readFixture("world-test-1.world.yaml"));

  map.loadWorldFromYaml(resource);
  require(map.getWorld() != nullptr, "Valid world did not load");

  resource->mText = "world: [";
  bool threw = false;
  try {
    map.loadWorldFromYaml(resource);
  } catch (std::exception const&) {
    threw = true;
  }

  require(threw, "Malformed world did not fail to load");
  require(map.getWorld() != nullptr,
          "Failed world load replaced the previous valid World");
}

std::string serializeBinaryWorldWithOnePrimitive() {
  bw::core::World world(1000.0f, 512.0f);
  world.addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 1.0f));

  auto serializer = std::shared_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::toString());
  auto workData = bw::core::SerializationWorkData{};
  world.serialize(serializer, workData);

  return serializer->getSerializedString();
}

void resourcesWithAWorldExtensionLoadAsBinary() {
  auto data = serializeBinaryWorldWithOnePrimitive();

  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);
  map.loadWorldFromYaml(makeWorldResource(data, "stress-test.world"));

  require(map.getWorld() != nullptr, "Binary .world resource did not load");
  require(map.getWorld()->getNumPrimitives() == 1,
          "Binary .world resource did not preserve its primitives");
}

void yamlWorldsWithoutTheWorldYamlExtensionAreRejected() {
  auto const yaml = readFixture("world-test-1.world.yaml");

  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);

  bool threw = false;
  try {
    map.loadWorldFromYaml(makeWorldResource(yaml, "stress-test.yaml"));
  } catch (std::exception const&) {
    threw = true;
  }

  require(threw, "a YAML World without the .world.yaml extension was accepted");
}

void minesCreateOneSixCellFloorVariationPerTunnelSection() {
  bw::core::ScriptRuntime runtime;
  runtime.load(
      "MinesLayer", readFixture("scripts/mines-layer.lua"),
      {{"World/UtilityFunctions", readFixture("scripts/utility-functions.lua")}},
      {{.name = "iterations",
        .type = bw::core::BuildVariableType::Integer,
        .defaultValue = int64_t{10},
        .integerMinimum = 1,
        .integerMaximum = 50}});
  bw::core::registerScriptStepTypes(runtime);

  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger, &runtime);
  map.loadWorldFromYaml(
      makeWorldResource(readFixture("world-mines-3.world.yaml")));

  auto* layer = map.getWorld()->getActiveLayer();
  uint32_t scriptStepIndex = ~0u;
  bw::core::RunScript* scriptStep = nullptr;
  for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
    if (auto* candidate =
            dynamic_cast<bw::core::RunScript*>(layer->getStep(i))) {
      scriptStepIndex = i;
      scriptStep = candidate;
      break;
    }
  }
  require(scriptStep && !scriptStep->hasFailed(),
          "the mines RunScript did not build");

  std::vector<bw::core::Primitive*> tunnels;
  std::vector<bw::core::Primitive*> floorCells;
  std::vector<bw::core::Primitive*> bridges;
  std::vector<bw::core::Primitive*> posts;
  for (auto* primitive : layer->getPrimitives()) {
    if (layer->getOwningStepIndex(primitive) != scriptStepIndex) continue;
    auto const size = primitive->getSize();
    if (primitive->getType() == "Mesh" && primitive->getPriority() == 0) {
      tunnels.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 1 && size.x >= 24.0f &&
               size.y >= 24.0f) {
      floorCells.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 1) {
      bridges.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 2) {
      posts.push_back(primitive);
    }
  }

  require(!tunnels.empty() && floorCells.size() == tunnels.size() * 6,
          "a tunnel section did not create exactly six floor-variation cells");
  auto const baseFloor = tunnels.front()->getProperties().floorSpan;
  for (auto const* cell : floorCells) {
    auto const floor = cell->getProperties().floorSpan;
    auto const offset = floor.lowerElevation - baseFloor.lowerElevation;
    require((offset == -4.0f || offset == 4.0f) &&
                floor.upperElevation == baseFloor.upperElevation + offset,
            "a floor-variation cell was not raised or lowered by four");
  }

  for (auto const* bridge : bridges) {
    auto expectedFloor = baseFloor;
    for (auto const* cell : floorCells) {
      if (cell->getPickingTriangulation().pointInside(bridge->getPosition())) {
        expectedFloor = cell->getProperties().floorSpan;
        break;
      }
    }
    require(bridge->getProperties().floorSpan == expectedFloor,
            "a wooden bridge did not inherit its cell's floor height");
  }

  for (auto const* post : posts) {
    bw::core::Primitive const* nearestBridge = nullptr;
    auto nearestDistance = std::numeric_limits<float>::max();
    for (auto const* bridge : bridges) {
      auto const distance =
          post->getPosition().distanceToSq(bridge->getPosition());
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestBridge = bridge;
      }
    }
    require(nearestBridge && nearestDistance < 13.0f * 13.0f &&
                post->getProperties().floorSpan ==
                    nearestBridge->getProperties().floorSpan,
            "a wooden post did not inherit its frame's floor height");
  }
}

void theGameResolvesAndRunsAWorldsLuaScript() {
  namespace resources = wp::application::resourcesystem;

  auto const unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
  auto root = std::filesystem::temp_directory_path() /
              ("boolean-world-map-lua-script-resource-" +
               std::to_string(unique));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream script(root / "script.lua");
    script << R"(local p = context:create_primitive("Rectangle")
                  p:set_size(8, 8)
                  context:place_primitive(p))";
    std::ofstream manifest(root / "Resources.yaml");
    manifest << R"(Resources:
  Namespace:
    name: "World"
    Resource:
      type: "LuaScript"
      name: "MapScript"
      location: "script.lua"
)";
  }

  bw::core::ScriptRuntime sourceRuntime;
  sourceRuntime.load("MapScript", R"(local p = context:create_primitive("Rectangle")
                                      p:set_size(8, 8)
                                      context:place_primitive(p))");
  bw::core::World source(512.0f, 16.0f);
  auto* sourceStep = new bw::core::RunScript(sourceRuntime);
  sourceStep->setScriptName("MapScript");
  source.getActiveLayer()->addStep(sourceStep);
  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  auto workData = bw::core::SerializationWorkData{};
  source.serialize(writer, workData);

  wp::Logger logger;
  resources::ResourceManager manager(nullptr, nullptr, nullptr, &logger);
  manager.addResourceLocationFactory(
      "Directory",
      [&logger](std::string const& path,
                std::string const& definition) -> resources::ResourceLocation* {
        return new resources::DirectoryResourceLocation(
            &logger, path, definition);
      });
  bw::core::registerLuaScriptResourceType(manager);
  manager.addResourceLocation("Directory", root.string(), "Resources.yaml");
  manager.scanLocations();

  bw::core::ScriptRuntime gameRuntime;
  bw::core::registerScriptStepTypes(gameRuntime);
  Map map("map", "World", "", {}, nullptr, &logger, &gameRuntime);
  map.loadWorldFromYaml(
      makeWorldResource(writer->getSerializedString()), &manager);

  require(gameRuntime.isLoaded("MapScript"),
          "the game did not compile the resolved LuaScript");
  require(map.getWorld()->getActiveLayer()->getNumPrimitives() == 1,
          "the game did not run the World resource's RunScript step");
  std::filesystem::remove_all(root);
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    playMapsUseDynamicWorldDataGenerators();
    establishedWorldEnablesAndRoundTripsWedges();
    failedLoadRetainsThePreviousWorld();
    resourcesWithAWorldExtensionLoadAsBinary();
    yamlWorldsWithoutTheWorldYamlExtensionAreRejected();
    minesCreateOneSixCellFloorVariationPerTunnelSection();
    theGameResolvesAndRunsAWorldsLuaScript();
    std::cout << "Map failed-load ownership regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
