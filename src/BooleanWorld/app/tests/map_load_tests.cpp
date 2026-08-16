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
    std::string const& text) {
  auto resource = std::make_shared<wp::application::resourcesystem::TextFileResource>(
      "world", "", "", std::map<std::string, std::string>{}, nullptr);
  resource->mText = text;
  return resource;
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
}  // namespace

int main() {
  try {
    failedLoadReleasesThePreviousWorld();
    std::cout << "Map failed-load ownership regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
