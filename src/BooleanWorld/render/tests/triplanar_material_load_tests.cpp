#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

#include "TriplanarMaterial.h"
#include "TriplanarMaterialResourceDefinitionFactory.h"

namespace {
namespace fs = std::filesystem;
using wp::application::resourcesystem::DirectoryResourceLocation;
using wp::application::resourcesystem::ResourceLocation;
using wp::application::resourcesystem::ResourceManager;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void writeFile(fs::path const& path, std::string const& contents) {
  std::ofstream output(path);
  output << contents;
  if (!output) throw std::runtime_error("Could not write " + path.string());
}

std::unique_ptr<ResourceManager> makeManager(
    fs::path const& root, wp::Logger& logger) {
  auto manager =
      std::make_unique<ResourceManager>(nullptr, nullptr, nullptr, &logger);
  manager->addResourceLocationFactory(
      "Directory",
      [&logger](std::string const& path,
                std::string const& definition) -> ResourceLocation* {
        return new DirectoryResourceLocation(&logger, path, definition);
      });
  manager->addResourceFactory(new TriplanarMaterialResourceFactory());
  manager->addResourceDefinitionFactory(
      new TriplanarMaterialResourceDefinitionFactory());
  manager->addResourceLocation("Directory", root.string(), "Resources.yaml");
  return manager;
}

std::string manifest(
    std::string const& options, std::string const& definition,
    std::string const& dependencyType = "Image",
    std::string const& dependencyReference = "AlbedoImage") {
  auto location = dependencyType == "Image" ? "albedo.png" : "albedo.txt";
  return "Resources:\n"
         "  Resource:\n"
         "    - type: \"" +
         dependencyType + "\"\n"
         "      name: \"AlbedoImage\"\n"
         "      location: \"" +
         location + "\"\n" + options +
         "    - type: \"TriplanarMaterial\"\n"
         "      name: \"TestMaterial\"\n"
         "      DependentResources:\n"
         "        DependentResource:\n"
         "          id: \"Albedo\"\n"
         "          ref: \"" +
         dependencyReference + "\"\n"
         "      Definitions:\n"
         "        Definition:\n" + definition;
}

void prepareImage(fs::path const& root) {
  fs::copy_file(
      BW_TRIPLANAR_TEST_IMAGE, root / "albedo.png",
      fs::copy_options::overwrite_existing);
  writeFile(root / "albedo.txt", "not an image");
}

struct CreatedMaterial {
  std::unique_ptr<ResourceManager> manager;
  std::shared_ptr<TriplanarMaterial> material;
};

CreatedMaterial createMaterial(
    fs::path const& root, wp::Logger& logger, std::string const& yaml) {
  writeFile(root / "Resources.yaml", yaml);
  auto manager = makeManager(root, logger);
  manager->scanLocations();
  manager->createAllResources();
  auto resource = manager->getQualifiedResource("TestMaterial");
  auto material = std::dynamic_pointer_cast<TriplanarMaterial>(resource);
  require(material != nullptr, "TriplanarMaterial factory returned the wrong type");
  return {std::move(manager), std::move(material)};
}

void defaultsAreApplied(fs::path const& root, wp::Logger& logger) {
  auto created = createMaterial(
      root, logger,
      manifest("      Option:\n        name: \"wrapping\"\n        value: \"repeat\"\n",
               "          Albedo: \"Albedo\"\n"));
  auto const& material = created.material;
  require(material->getAlbedo() != nullptr,
          "Triplanar material did not retain its albedo dependency");
  require(material->getTileWidth() == 32.0f &&
              material->getBlendSharpness() == 4.0f,
          "Triplanar material defaults changed");
  require(std::abs(material->getTileHeight() - 16.0f) < 0.0001f,
          "non-square albedo did not preserve square texels");
  require(material->dependsOn(material->getAlbedo().get()),
          "albedo is not a transitive dependency of its Triplanar material");
}

void explicitBoundsAreAccepted(fs::path const& root, wp::Logger& logger) {
  auto created = createMaterial(
      root, logger,
      manifest("      Option:\n        name: \"wrapping\"\n        value: \"repeat\"\n",
               "          Albedo: \"Albedo\"\n"
               "          TileWidth: \"0.25\"\n"
               "          BlendSharpness: \"16\"\n"));
  require(created.material->getTileWidth() == 0.25f &&
              created.material->getBlendSharpness() == 16.0f,
          "valid inclusive Triplanar bounds were not retained");
}

void requireCreationFailure(
    fs::path const& root, wp::Logger& logger, std::string const& yaml,
    std::string const& expected) {
  writeFile(root / "Resources.yaml", yaml);
  auto manager = makeManager(root, logger);
  bool threw = false;
  try {
    manager->scanLocations();
    manager->createAllResources();
  } catch (std::exception const& error) {
    threw = true;
    require(std::string(error.what()).find(expected) != std::string::npos,
            "Unexpected resource error: " + std::string(error.what()));
  }
  require(threw, "invalid Triplanar material loaded successfully");
}

std::string fieldDefinition(char const* field, char const* value) {
  return std::string("          Albedo: \"Albedo\"\n          ") + field +
         ": \"" + value + "\"\n";
}

void invalidNumberFails(
    fs::path const& root, wp::Logger& logger, char const* field,
    char const* value) {
  requireCreationFailure(
      root, logger,
      manifest("      Option:\n        name: \"wrapping\"\n        value: \"repeat\"\n",
               fieldDefinition(field, value)),
      std::string(field) + " must be finite and in the inclusive range");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: triplanar_material_load_tests <scenario>\n";
    return 2;
  }

  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  auto root = fs::temp_directory_path() /
              ("boolean-world-triplanar-material-" + std::to_string(unique));
  fs::create_directories(root);

  try {
    wp::Logger logger;
    prepareImage(root);
    std::string scenario = argv[1];
    if (scenario == "defaults") {
      defaultsAreApplied(root, logger);
    } else if (scenario == "explicit") {
      explicitBoundsAreAccepted(root, logger);
    } else if (scenario == "invalid-tile-low") {
      invalidNumberFails(root, logger, "TileWidth", "0.249");
    } else if (scenario == "invalid-tile-high") {
      invalidNumberFails(root, logger, "TileWidth", "1024.1");
    } else if (scenario == "invalid-tile-nan") {
      invalidNumberFails(root, logger, "TileWidth", "nan");
    } else if (scenario == "invalid-sharpness-low") {
      invalidNumberFails(root, logger, "BlendSharpness", "0.999");
    } else if (scenario == "invalid-sharpness-high") {
      invalidNumberFails(root, logger, "BlendSharpness", "16.1");
    } else if (scenario == "invalid-sharpness-infinite") {
      invalidNumberFails(root, logger, "BlendSharpness", "inf");
    } else if (scenario == "missing-image") {
      requireCreationFailure(
          root, logger,
          manifest("", "          Albedo: \"Albedo\"\n", "Image", "Missing"),
          "Missing");
    } else if (scenario == "wrong-type") {
      requireCreationFailure(
          root, logger,
          manifest("", "          Albedo: \"Albedo\"\n", "TextFile"),
          "not an ImageResource");
    } else if (scenario == "non-repeat") {
      requireCreationFailure(
          root, logger,
          manifest("", "          Albedo: \"Albedo\"\n"),
          "must use repeat wrapping");
    } else {
      throw std::runtime_error("Unknown scenario: " + scenario);
    }
    fs::remove_all(root);
    std::cout << "Triplanar material scenario passed: " << scenario << '\n';
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
