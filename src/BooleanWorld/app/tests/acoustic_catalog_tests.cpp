#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

#include "AcousticCatalog.h"
#include "AcousticCatalogResourceDefinitionFactory.h"
#include "AcousticPresetResolver.h"
#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"

namespace {
namespace fs = std::filesystem;
using wp::application::resourcesystem::DirectoryResourceLocation;
using wp::application::resourcesystem::ResourceLocation;
using wp::application::resourcesystem::ResourceManager;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void writeFile(fs::path const& path, std::string const& contents) {
  std::ofstream stream(path);
  stream << contents;
  if (!stream) throw std::runtime_error("Could not write " + path.string());
}

void catalogLoadsAndResolverFallsBack(fs::path const& root, wp::Logger& logger) {
  writeFile(root / "acoustics.yaml", R"(presets:
  - id: builtin.acoustic.generic
    name: Generic
    absorption: [0.10, 0.20, 0.30]
    scattering: 0.05
    transmission: [0.100, 0.050, 0.030]
  - id: test.stone
    name: Test Stone
    absorption: [0.13, 0.20, 0.24]
    scattering: 0.15
    transmission: [0.015, 0.002, 0.001]
)");
  writeFile(root / "materials.yaml", R"(program3d: unused
program2d: unused
techniqueSchemas:
  - materialIndex: 0
    parameters: []
subMaterials:
  - id: test.visual.stone
    name: Visual Stone
    acousticPreset: test.stone
    materialIndex: 0
    params: []
    baseColour: [0.5, 0.5, 0.5]
)");
  writeFile(root / "Resources.yaml", R"(Resources:
  Resource:
    - type: TextFile
      name: AcousticCatalogFile
      location: acoustics.yaml
    - type: AcousticCatalog
      name: Acoustics
      DependentResources:
        DependentResource:
          id: Yaml
          ref: AcousticCatalogFile
      Definitions:
        Definition:
          Resource: Yaml
    - type: TextFile
      name: ProcMaterialFile
      location: materials.yaml
    - type: ProcMaterial
      name: ProcMaterials
      DependentResources:
        DependentResource:
          id: Yaml
          ref: ProcMaterialFile
      Definitions:
        Definition:
          Resource: Yaml
)");

  ResourceManager manager(nullptr, nullptr, nullptr, &logger);
  manager.addResourceLocationFactory(
      "Directory", [&logger](std::string const& path, std::string const& definition) -> ResourceLocation* {
        return new DirectoryResourceLocation(&logger, path, definition);
      });
  manager.addResourceFactory(new AcousticCatalogResourceFactory());
  manager.addResourceDefinitionFactory(
      new AcousticCatalogResourceDefinitionFactory());
  manager.addResourceFactory(new ProcMaterialResourceFactory());
  manager.addResourceDefinitionFactory(
      new ProcMaterialResourceDefinitionFactory());
  manager.addResourceLocation("Directory", root.string(), "Resources.yaml");
  manager.scanLocations();
  manager.createAllResources();
  manager.loadAllResources(false);

  AcousticPresetResolver resolver(&manager);
  auto const& stone = resolver.resolve("test.stone");
  require(stone.id == "test.stone" && stone.absorption[0] == 0.13f &&
              stone.scattering == 0.15f,
          "authored Acoustic preset did not resolve");
  require(resolver.resolveSubMaterial("test.visual.stone") == stone,
          "Sub-material did not resolve through its Acoustic preset id");
  require(resolver.resolveSurfaceMaterial(
              bw::core::SurfaceMaterialReference::subMaterial(
                  "test.visual.stone")) == stone,
          "tagged Sub-material acoustic resolution changed");
  require(resolver.resolveSurfaceMaterial(
              bw::core::SurfaceMaterialReference::triplanar(
                  "test.visual.stone")) ==
              AcousticPresetResolver::defaultPreset(),
          "Triplanar material inferred an acoustic choice from a colliding Sub-material id");

  auto const& missing = resolver.resolve("no.such.preset");
  auto const& empty = resolver.resolve("");
  require(missing == AcousticPresetResolver::defaultPreset() &&
              empty == AcousticPresetResolver::defaultPreset() &&
              missing.id == AcousticPresetResolver::DefaultPresetId,
          "unresolved Acoustic preset did not use the documented default");
}

}  // namespace

int main() {
  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-acoustics-" + std::to_string(unique));
  fs::create_directories(root);
  try {
    wp::Logger logger;
    catalogLoadsAndResolverFallsBack(root, logger);
    fs::remove_all(root);
    std::cout << "Acoustic catalog coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
