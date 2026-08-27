#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <willpower/common/Logger.h>
#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/Defines.h>

#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"
#include "SubMaterialResolver.h"

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

// A Sub-material id present in a loaded ProcMaterial resolves to the
// Technique, parameters and base colour authored for it - see issue #261.
void resolveFindsAnAuthoredSubMaterial(fs::path const& root, wp::Logger& logger) {
  writeFile(root / "proc-materials.yaml", R"(program3d: "World/WorldProgram"
program2d: "World/WorldHorizontal2dProgram"
techniqueSchemas:
  - materialIndex: 5
    parameters:
      - name: "roughness"
        min: 0.0
        max: 1.0
        default: 0.5
subMaterials:
  - id: "TestStone"
    name: "Test Stone"
    materialIndex: 5
    params: [0.75]
    baseColour: [0.2, 0.4, 0.6]
    chip:
      minimumArrisLength: 4
      minimumDepth: 1.5
      maximumDepth: 2.5
      minimumReach: 2
      maximumReach: 4
      minimumSpacing: 4.1
      probability: 0.65
      minimumCornerDistance: 0.75
      maximumCornerDistance: 2.75
      cornerProbability: 0.4
      types: [PrismaticNotch, MultiFacetSpall, VShapedNotch]
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

  ResourceManager manager(nullptr, nullptr, nullptr, &logger);
  manager.addResourceLocationFactory(
      "Directory", [&logger](std::string const& path, std::string const& definition) -> ResourceLocation* {
        return new DirectoryResourceLocation(&logger, path, definition);
      });
  manager.addResourceFactory(new ProcMaterialResourceFactory());
  manager.addResourceDefinitionFactory(new ProcMaterialResourceDefinitionFactory());
  manager.addResourceLocation("Directory", root.string(), "Resources.yaml");
  manager.scanLocations();
  manager.createAllResources();
  manager.loadAllResources(false);

  SubMaterialResolver resolver(&manager);

  auto resolved = resolver.resolve("TestStone");
  require(resolved.materialIndex == 5, "Expected the authored materialIndex");
  require(resolved.def.params[0] == 0.75f, "Expected the authored parameter value");
  require(resolved.def.params[1] == 0.0f, "Expected unauthored parameter slots to be zeroed");
  require(resolved.def.baseColour == std::array<float, 3>{0.2f, 0.4f, 0.6f}, "Expected the authored base colour");
  require(resolved.chipParameters.maximumDepth == 2.5f &&
              resolved.chipParameters.maximumReach == 4.0f &&
              resolved.chipParameters.probability == 0.65f &&
              resolved.chipParameters.minimumCornerDistance == 0.75f &&
              resolved.chipParameters.maximumCornerDistance == 2.75f &&
              resolved.chipParameters.cornerProbability == 0.4f &&
              resolved.chipParameters.types ==
                  std::vector<bw::core::ChipType>{
                      bw::core::ChipType::PrismaticNotch,
                      bw::core::ChipType::MultiFacetSpall,
                      bw::core::ChipType::VShapedNotch},
          "Expected the authored Chip generation parameters");
  auto chipResolver = resolver.chipParametersResolver();
  auto chip = chipResolver("TestStone");
  require(chip == resolved.chipParameters,
          "Expected the generator callback to carry Chip parameters");
}

// An id that names no Sub-material in any loaded ProcMaterial - missing,
// unknown, or an empty/unresolved slot - falls back to a clearly
// out-of-range error index rather than aliasing a real Technique.
void resolveFallsBackForAnUnknownId(fs::path const& root, wp::Logger& logger) {
  writeFile(root / "Resources.yaml", "Resources:\n");

  ResourceManager manager(nullptr, nullptr, nullptr, &logger);
  manager.addResourceLocationFactory(
      "Directory", [&logger](std::string const& path, std::string const& definition) -> ResourceLocation* {
        return new DirectoryResourceLocation(&logger, path, definition);
      });
  manager.addResourceLocation("Directory", root.string(), "Resources.yaml");
  manager.scanLocations();

  SubMaterialResolver resolver(&manager);

  auto resolved = resolver.resolve("NoSuchSubMaterial");
  require(resolved.materialIndex == BW_MATERIAL_ERROR_INDEX, "Expected the error index for an unknown id");

  auto resolvedEmpty = resolver.resolve("");
  require(resolvedEmpty.materialIndex == BW_MATERIAL_ERROR_INDEX, "Expected the error index for an empty id");
  auto chip = resolver.chipParametersResolver()("NoSuchSubMaterial");
  require(chip.probability == 0.0f && chip.cornerProbability == 0.0f,
          "Expected an unknown Sub-material not to chip");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: sub_material_resolver_tests <scenario>\n";
    return 2;
  }

  auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() /
                  ("boolean-world-sub-material-resolver-" + std::to_string(unique));
  fs::create_directories(root);

  try {
    wp::Logger logger;
    std::string scenario = argv[1];
    if (scenario == "resolved") {
      resolveFindsAnAuthoredSubMaterial(root, logger);
    } else if (scenario == "fallback") {
      resolveFallsBackForAnUnknownId(root, logger);
    } else {
      throw std::runtime_error("Unknown scenario: " + scenario);
    }

    fs::remove_all(root);
    std::cout << "SubMaterialResolver scenario passed: " << scenario << '\n';
    return 0;
  } catch (std::exception const& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
