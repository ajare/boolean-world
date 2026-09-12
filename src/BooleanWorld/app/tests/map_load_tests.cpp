#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
#include <core/MeshPrimitive.h>
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

void minesCreateLevelRailRunsAndWoodenSupports() {
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
  std::vector<bw::core::Primitive*> rails;
  std::vector<bw::core::Primitive*> railStops;
  std::vector<bw::core::Primitive*> bridges;
  std::vector<bw::core::Primitive*> posts;
  for (auto* primitive : layer->getPrimitives()) {
    if (layer->getOwningStepIndex(primitive) != scriptStepIndex) continue;
    if (primitive->getType() == "Mesh" && primitive->getPriority() == 0) {
      tunnels.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 1) {
      rails.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 4) {
      railStops.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 2) {
      bridges.push_back(primitive);
    } else if (primitive->getType() == "Rectangle" &&
               primitive->getPriority() == 3) {
      posts.push_back(primitive);
    }
  }

  require(!tunnels.empty(), "the mines script produced no tunnel floors");
  require(!rails.empty() && rails.size() % 2 == 0,
          "rails_pct 100 did not produce complete rail pairs");
  for (auto const* rail : rails) {
    auto const size = rail->getSize();
    require(size.y == 1.0f && size.x >= 32.0f && size.x <= 160.0f &&
                std::fmod(size.x, 32.0f) == 0.0f,
            "a rail did not span two to six cell centres at the configured width");

    bw::core::Primitive const* containingFloor = nullptr;
    for (auto const* tunnel : tunnels) {
      if (tunnel->getPickingTriangulation().pointInside(rail->getPosition())) {
        containingFloor = tunnel;
        break;
      }
    }
    auto const& properties = rail->getProperties();
    require(containingFloor, "a rail was not contained by a tunnel floor");
    require(std::abs(properties.floorSpan.lowerElevation -
                     containingFloor->getProperties().floorSpan.lowerElevation -
                     1.0f) < 0.0001f &&
                properties.floorSpan.upperElevation ==
                    properties.floorSpan.lowerElevation,
            "a rail was not raised one unit above a level tunnel floor");
    require(properties.ceilingSpan ==
                containingFloor->getProperties().ceilingSpan,
            "a rail did not inherit its tunnel ceiling elevation");
    require(properties.floorMaterial.reference == "builtin.rusted.iron" &&
                properties.wallMaterial.reference == "builtin.rusted.iron" &&
                properties.ceilingMaterial.reference == "builtin.basalt",
            "a rail did not use the configured materials");

    bool foundPartner = false;
    for (auto const* candidate : rails) {
      if (candidate == rail || candidate->getSize() != rail->getSize() ||
          candidate->getOrientation() != rail->getOrientation()) {
        continue;
      }
      if (std::abs(candidate->getPosition().distanceToSq(rail->getPosition()) -
                   64.0f) < 0.0001f) {
        foundPartner = true;
        break;
      }
    }
    require(foundPartner, "a rail did not have an evenly spaced partner");
  }

  require(!railStops.empty() && railStops.size() <= rails.size(),
          "the configured rail runs did not produce plausible end stops");
  for (auto const* stop : railStops) {
    auto const size = stop->getSize();
    auto const& properties = stop->getProperties();
    require(size.x == 8.0f && size.y == 12.0f,
            "a rail stop did not have the configured footprint");
    auto direction = std::fmod(properties.floorSpan.directionAngle, 360.0f);
    if (direction < 0.0f) direction += 360.0f;
    require(std::abs(properties.floorSpan.upperElevation -
                     properties.floorSpan.lowerElevation - 8.0f) < 0.0001f &&
                (direction == 90.0f || direction == 270.0f),
            "a rail stop was not an 8-unit triangular wedge");
    require(properties.floorMaterial.reference == "builtin.basalt" &&
                properties.wallMaterial.reference == "builtin.basalt" &&
                properties.ceilingMaterial.reference == "builtin.basalt",
            "a rail stop did not use the mine floor material");
  }

  for (auto const [cellX, cellY] :
       std::array<std::array<int, 2>, 3>{{{1, 0}, {-1, 0}, {1, -1}}}) {
    bool foundTunnelSection = false;
    for (auto sampleY = 0; sampleY < 8 && !foundTunnelSection; ++sampleY) {
      for (auto sampleX = 0; sampleX < 8 && !foundTunnelSection; ++sampleX) {
        auto const point = wp::Vector2{
            cellX * 256.0f + (sampleX + 0.5f) * 32.0f,
            cellY * 256.0f + (sampleY + 0.5f) * 32.0f};
        foundTunnelSection = std::any_of(
            tunnels.begin(), tunnels.end(), [point](auto const* tunnel) {
              return tunnel->getPickingTriangulation().pointInside(point);
            });
      }
    }
    require(foundTunnelSection,
            "rail bounds incorrectly prevented a neighbouring tunnel section");
  }

  bool foundVariedFloor = false;
  bool foundCornerPool = false;
  bool foundSlopedTransition = false;
  for (auto const* tunnel : tunnels) {
    auto const* mesh = dynamic_cast<bw::core::MeshPrimitive const*>(tunnel);
    require(mesh && mesh->getShells().size() == 1,
            "a sliced tunnel floor was not decomposed by polygon");
    auto const floor = tunnel->getProperties().floorSpan;
    foundVariedFloor |= floor.lowerElevation != 0.0f;
    auto const liquidLevel = tunnel->getProperties().liquidLevel;
    if (liquidLevel > 0.0f) {
      auto cornerAngle = std::fmod(floor.directionAngle, 360.0f);
      if (cornerAngle < 0.0f) cornerAngle += 360.0f;
      auto const pointsTowardCorner = cornerAngle == 45.0f ||
                                      cornerAngle == 135.0f ||
                                      cornerAngle == 225.0f ||
                                      cornerAngle == 315.0f;
      auto const poolVertices = mesh->getShells().front().ring.size();
      require(liquidLevel == 8.0f && pointsTowardCorner &&
                  poolVertices >= 5 &&
                  std::abs(floor.upperElevation - floor.lowerElevation -
                           3.0f) < 0.0001f,
              "a corner pool did not curve and slope toward its corner");
      foundCornerPool = true;
    } else {
      auto direction = std::fmod(floor.directionAngle, 360.0f);
      if (direction < 0.0f) direction += 360.0f;
      auto const cardinalSlope = direction == 0.0f || direction == 90.0f ||
                                 direction == 180.0f || direction == 270.0f;
      require(floor.upperElevation == floor.lowerElevation || cardinalSlope,
              "a non-pool tunnel polygon had an invalid floor elevation");
      foundSlopedTransition |= floor.upperElevation != floor.lowerElevation;
    }
  }
  require(foundVariedFloor,
          "sliced tunnel polygons did not receive varied floor heights");
  require(foundCornerPool,
          "corner cells did not create lowered pool polygons");
  require(foundSlopedTransition,
          "connected mine sections did not create a sloped transition");
  require(!bridges.empty() && posts.size() == bridges.size() * 2,
          "the configured wooden support percentage produced no complete frames");
  for (auto const* bridge : bridges) {
    bw::core::Primitive const* containingFloor = nullptr;
    for (auto const* tunnel : tunnels) {
      if (tunnel->getPickingTriangulation().pointInside(
              bridge->getPosition())) {
        containingFloor = tunnel;
        break;
      }
    }
    require(containingFloor &&
                bridge->getProperties().floorSpan ==
                    containingFloor->getProperties().floorSpan,
            "a wooden bridge did not use its tunnel polygon's floor height");
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
    minesCreateLevelRailRunsAndWoodenSupports();
    theGameResolvesAndRunsAWorldsLuaScript();
    std::cout << "Map failed-load ownership regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
