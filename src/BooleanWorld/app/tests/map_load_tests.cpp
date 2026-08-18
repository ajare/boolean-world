#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <willpower/application/resourcesystem/Resource.h>

#define class struct
#include <willpower/application/resourcesystem/TextFileResource.h>
#undef class

#include <core/BinarySerializer.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

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
    std::string const& text, std::string const& source = "") {
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
  map.loadWorldFromYaml(makeWorldResource(readFixture("basic-test.yaml")));

  require(dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
              map.getWorld()->getWorldDataGenerator()) != nullptr,
          "Loaded play map did not install a dynamic world data generator");
}

void failedLoadReleasesThePreviousWorld() {
  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);
  auto resource = makeWorldResource(readFixture("basic-test.yaml"));

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
  require(map.getWorld() == nullptr,
          "Failed world load retained a dangling World pointer");
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

void resourcesWithoutAWorldExtensionAreParsedAsYaml() {
  auto data = serializeBinaryWorldWithOnePrimitive();

  wp::Logger logger;
  Map map("map", "", "", {}, nullptr, &logger);

  bool threw = false;
  try {
    // Binary bytes are not valid YAML, so a resource whose source doesn't
    // end in .world must still be parsed as YAML and should fail to load.
    map.loadWorldFromYaml(makeWorldResource(data, "stress-test.yaml"));
  } catch (std::exception const&) {
    threw = true;
  }

  require(threw, "Binary content sourced as .yaml was not rejected");
}
}  // namespace

int main() {
  try {
    playMapsUseDynamicWorldDataGenerators();
    failedLoadReleasesThePreviousWorld();
    resourcesWithAWorldExtensionLoadAsBinary();
    resourcesWithoutAWorldExtensionAreParsedAsYaml();
    std::cout << "Map failed-load ownership regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
