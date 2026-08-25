#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <willpower/common/Logger.h>
#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"

namespace {
namespace fs = std::filesystem;
using wp::application::resourcesystem::DirectoryResourceLocation;
using wp::application::resourcesystem::ResourceLocation;
using wp::application::resourcesystem::ResourceManager;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeFile(fs::path const& path, std::string const& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << contents;
  if (!stream) throw std::runtime_error("Could not write test fixture: " + path.string());
}

std::unique_ptr<ResourceManager> makeManager(fs::path const& root, wp::Logger& logger) {
  auto manager = std::make_unique<ResourceManager>(nullptr, nullptr, nullptr, &logger);

  manager->addResourceLocationFactory(
      "Directory", [&logger](std::string const& path, std::string const& definition) -> ResourceLocation* {
        return new DirectoryResourceLocation(&logger, path, definition);
      });

  manager->addResourceFactory(new ProcMaterialResourceFactory());
  manager->addResourceDefinitionFactory(new ProcMaterialResourceDefinitionFactory());

  manager->addResourceLocation("Directory", root.string(), "Resources.yaml");

  return manager;
}

// A fixture ProcMaterial YAML file, declared in Resources.yaml with the same
// TextFile dependent-resource shape Map uses, loads end to end through a
// real ResourceManager - see issue #260's acceptance criteria.
void fixtureLoadsThroughAResourceManager(fs::path const& root, wp::Logger& logger) {
  writeFile(root / "proc-materials.yaml", R"(program3d: "World/WorldProgram"
program2d: "World/WorldHorizontal2dProgram"
techniqueSchemas:
  - materialIndex: 0
    parameters:
      - name: "roughness"
        min: 0.0
        max: 1.0
        default: 0.5
subMaterials:
  - id: "TestStone"
    name: "Test Stone"
    materialIndex: 0
    params: [0.5]
    baseColour: [0.5, 0.5, 0.5]
)");

  writeFile(root / "Resources.yaml", R"(Resources:
  Resource:
    - type: "TextFile"
      name: "ProcMaterialsFile"
      location: "proc-materials.yaml"
    - type: "ProcMaterial"
      name: "ProcMaterials"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "ProcMaterialsFile"
      Definitions:
        Definition:
          Resource: "Yaml"
)");

  auto manager = makeManager(root, logger);
  manager->scanLocations();
  manager->createAllResources();
  manager->loadAllResources(false);

  auto resource = manager->getQualifiedResource("ProcMaterials");
  auto procMaterial = static_cast<ProcMaterial*>(resource.get());
  auto const& data = procMaterial->getData();

  require(data.program3d == "World/WorldProgram", "program3d did not round-trip");
  require(data.techniqueSchemas.size() == 1, "Expected one TechniqueSchema");
  require(data.subMaterials.size() == 1, "Expected one SubMaterial");
  require(data.subMaterials[0].id == "TestStone", "Expected SubMaterial id 'TestStone'");
}

// Two catalogs that are each internally valid, but that share a SubMaterial
// id, must fail to load: id uniqueness is global across every loaded
// ProcMaterial resource, not merely within one catalog - see ADR-0023.
void duplicateSubMaterialIdAcrossCatalogsFailsToLoad(fs::path const& root, wp::Logger& logger) {
  writeFile(root / "proc-materials-a.yaml", R"(program3d: "World/WorldProgram"
program2d: "World/WorldHorizontal2dProgram"
techniqueSchemas:
  - materialIndex: 0
    parameters: []
subMaterials:
  - id: "Shared"
    name: "A"
    materialIndex: 0
    params: []
    baseColour: [0.0, 0.0, 0.0]
)");

  writeFile(root / "proc-materials-b.yaml", R"(program3d: "World/WorldProgram"
program2d: "World/WorldHorizontal2dProgram"
techniqueSchemas:
  - materialIndex: 0
    parameters: []
subMaterials:
  - id: "Shared"
    name: "B"
    materialIndex: 0
    params: []
    baseColour: [0.0, 0.0, 0.0]
)");

  writeFile(root / "Resources.yaml", R"(Resources:
  Resource:
    - type: "TextFile"
      name: "ProcMaterialsAFile"
      location: "proc-materials-a.yaml"
    - type: "ProcMaterial"
      name: "ProcMaterialsA"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "ProcMaterialsAFile"
      Definitions:
        Definition:
          Resource: "Yaml"
    - type: "TextFile"
      name: "ProcMaterialsBFile"
      location: "proc-materials-b.yaml"
    - type: "ProcMaterial"
      name: "ProcMaterialsB"
      DependentResources:
        DependentResource:
          id: "Yaml"
          ref: "ProcMaterialsBFile"
      Definitions:
        Definition:
          Resource: "Yaml"
)");

  auto manager = makeManager(root, logger);
  manager->scanLocations();

  bool threw = false;
  try {
    manager->createAllResources();
  } catch (wp::application::resourcesystem::ResourceException const& error) {
    threw = true;
    require(std::string(error.what()).find("Shared") != std::string::npos,
            "Unexpected error message: " + std::string(error.what()));
  }

  require(threw, "Duplicate Sub-material id across catalogs did not fail to load");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: proc_material_load_tests <scenario>\n";
    return 2;
  }

  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-proc-material-" + std::to_string(unique));
  fs::create_directories(root);

  try {
    wp::Logger logger;
    std::string scenario = argv[1];
    if (scenario == "fixture-load") {
      fixtureLoadsThroughAResourceManager(root, logger);
    } else if (scenario == "duplicate-id") {
      duplicateSubMaterialIdAcrossCatalogsFailsToLoad(root, logger);
    } else {
      throw std::runtime_error("Unknown scenario: " + scenario);
    }

    fs::remove_all(root);
    std::cout << "ProcMaterial load scenario passed: " << scenario << '\n';
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
