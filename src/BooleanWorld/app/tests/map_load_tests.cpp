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
#include <core/YamlSerializer.h>

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

  require(dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
              map.getWorld()->getWorldDataGenerator()) != nullptr,
          "Loaded play map did not install a dynamic world data generator");
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
}  // namespace

int main() {
  try {
    playMapsUseDynamicWorldDataGenerators();
    establishedWorldEnablesAndRoundTripsWedges();
    failedLoadRetainsThePreviousWorld();
    resourcesWithAWorldExtensionLoadAsBinary();
    yamlWorldsWithoutTheWorldYamlExtensionAreRejected();
    std::cout << "Map failed-load ownership regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
