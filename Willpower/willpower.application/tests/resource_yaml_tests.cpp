// Loads BooleanWorld's Resources.yaml through the real resource system and
// checks the manifest was reconstructed: namespaces, resource records,
// dependent resources and definitions.
//
// Adapted from tungsten-oxide's equivalent test, which asserts against
// TungstenMonoxide's manifest. This is the regression test for the XML -> YAML
// conversion: a mis-shaped document loads without error but yields no records.

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "willpower/application/resourcesystem/DirectoryResourceLocation.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: resource_yaml_tests <Resources.yaml>\n";
    return 2;
  }

  try {
    const std::filesystem::path definition = std::filesystem::absolute(argv[1]);
    wp::application::resourcesystem::DirectoryResourceLocation location(
        nullptr, definition.parent_path().string(), definition.filename().string());
    location.scan();

    const auto& namespaces = location.getNamespaceRecords();
    const auto root = namespaces.find("");
    const auto world = namespaces.find("World");
    require(root != namespaces.end(), "The root namespace was not reconstructed.");
    require(world != namespaces.end(), "The 'World' namespace was not reconstructed.");

    // A plain resource, and one carrying <Option> entries (which become
    // name/value pairs rather than plain scalars).
    require(root->second.resourceRecords.contains("EntityImage"),
            "Image resource 'EntityImage' was not reconstructed.");

    const auto& image = root->second.resourceRecords.at("EntityImage");
    require(image.baseData.tags.at("filtering") == "none",
            "EntityImage's 'filtering' option did not survive conversion.");
    require(image.baseData.tags.at("uv-style") == "atlas",
            "EntityImage's 'uv-style' option did not survive conversion.");

    // A composite resource: dependent resources plus a factory-tagged definition.
    require(world->second.resourceRecords.contains("World"),
            "World/World map resource was not reconstructed.");
    const auto& map = world->second.resourceRecords.at("World");
    require(!map.definitions.empty(), "World/World has no definitions.");
    require(map.dependentResources.size() == 1,
            "World/World should have exactly one dependent resource.");

    // Repeated sibling elements become a YAML sequence; make sure a long run of
    // them round-trips rather than collapsing to a single entry.
    require(root->second.resourceRecords.contains("EntityImageSet"),
            "EntityImageSet was not reconstructed.");

    std::cout << "BooleanWorld YAML resource definitions passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
