#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "core/Defines.h"

#include <common/MaterialRegistry.h>

#include "core/MaterialDefaultsFile.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.0001f;
}

void writeFile(std::string const& path, std::string const& contents) {
  std::ofstream file(path, std::ios::binary);
  file << contents;
}

void withoutAnyFileEverythingReadsTheCompiledDefault() {
  bw::core::clearMaterialDefaultOverrides();
  bw::core::loadMaterialDefaultsFile("this-file-does-not-exist.yaml");

  require(
      near(bw::core::materialParamDefault(0, 0),
           std::get<3>(bw::common::MaterialParams[0][0])),
      "a missing file changed a default that was never overridden");
  require(
      near(bw::core::materialParamMinimum(0, 0),
           std::get<1>(bw::common::MaterialParams[0][0])),
      "a missing file changed a minimum that was never overridden");
}

void anOverriddenParameterReadsFromTheFile() {
  bw::core::clearMaterialDefaultOverrides();
  writeFile(
      "material_defaults_file_test_override.yaml",
      "Configuration:\n"
      "  Materials:\n"
      "    - name: Marble\n"
      "      params:\n"
      "        - name: warp_scale\n"
      "          min: 0.5\n"
      "          max: 4.5\n"
      "          default: 2.0\n");
  bw::core::loadMaterialDefaultsFile("material_defaults_file_test_override.yaml");

  require(
      near(bw::core::materialParamDefault(0, 0), 2.0f),
      "the overridden default was not read from the file");
  require(
      near(bw::core::materialParamMinimum(0, 0), 0.5f),
      "the overridden minimum was not read from the file");
  require(
      near(bw::core::materialParamMaximum(0, 0), 4.5f),
      "the overridden maximum was not read from the file");

  // An untouched parameter of the same material must be unaffected.
  require(
      near(bw::core::materialParamDefault(0, 1),
           std::get<3>(bw::common::MaterialParams[0][1])),
      "overriding one parameter disturbed a sibling parameter");

  std::remove("material_defaults_file_test_override.yaml");
}

void anUnknownMaterialOrParameterNameIsIgnored() {
  bw::core::clearMaterialDefaultOverrides();
  writeFile(
      "material_defaults_file_test_unknown.yaml",
      "Configuration:\n"
      "  Materials:\n"
      "    - name: Obsidian\n"
      "      params:\n"
      "        - name: shininess\n"
      "          default: 99.0\n"
      "    - name: Marble\n"
      "      params:\n"
      "        - name: not_a_real_parameter\n"
      "          default: 99.0\n"
      "        - name: veins_scale\n"
      "          default: 7.5\n");
  bw::core::loadMaterialDefaultsFile("material_defaults_file_test_unknown.yaml");

  require(
      near(bw::core::materialParamDefault(0, 1), 7.5f),
      "the one real override amongst unknown names was not applied");
  // Nothing about the unknown entries should have thrown or otherwise
  // corrupted a real material's table - already implied by reaching here,
  // asserted anyway for a clear failure message if it regresses.
  require(
      near(bw::core::materialParamDefault(0, 0),
           std::get<3>(bw::common::MaterialParams[0][0])),
      "an unknown material name disturbed a real material");

  std::remove("material_defaults_file_test_unknown.yaml");
}

void clearRevertsEveryOverride() {
  bw::core::clearMaterialDefaultOverrides();
  writeFile(
      "material_defaults_file_test_clear.yaml",
      "Configuration:\n"
      "  Materials:\n"
      "    - name: Marble\n"
      "      params:\n"
      "        - name: warp_scale\n"
      "          default: 3.0\n");
  bw::core::loadMaterialDefaultsFile("material_defaults_file_test_clear.yaml");
  require(
      near(bw::core::materialParamDefault(0, 0), 3.0f),
      "the override did not apply before clearing");

  bw::core::clearMaterialDefaultOverrides();
  require(
      near(bw::core::materialParamDefault(0, 0),
           std::get<3>(bw::common::MaterialParams[0][0])),
      "clearMaterialDefaultOverrides left a stale override in place");

  std::remove("material_defaults_file_test_clear.yaml");
}

// The real, checked-in Game.yaml (deployed as a build-output sibling,
// exactly as the editor and the shipped game find it) rather than a
// synthetic fixture - this is what would actually catch a hand-edit to that
// file breaking its own YAML shape. Its Materials section is meant to match
// MaterialRegistry.h's compiled-in values exactly, so loading it must change
// nothing observable.
void theRealGameYamlParsesAndMatchesCompiledDefaults() {
  bw::core::clearMaterialDefaultOverrides();
  bw::core::loadMaterialDefaultsFile("../Launcher/Game.yaml");

  for (uint32_t materialIndex = 0; materialIndex < bw::common::MaterialNames.size();
       ++materialIndex) {
    auto declared = std::get<1>(bw::common::MaterialNames[materialIndex]);
    for (uint32_t paramIndex = 0; paramIndex < declared; ++paramIndex) {
      auto const& compiled =
          bw::common::MaterialParams[materialIndex][paramIndex];
      require(
          near(bw::core::materialParamDefault(materialIndex, paramIndex),
               std::get<3>(compiled)),
          "Game.yaml's Materials section default drifted from "
          "MaterialRegistry.h's compiled-in value for " +
              std::string(std::get<0>(compiled)));
      require(
          near(bw::core::materialParamMinimum(materialIndex, paramIndex),
               std::get<1>(compiled)) &&
              near(bw::core::materialParamMaximum(materialIndex, paramIndex),
                   std::get<2>(compiled)),
          "Game.yaml's Materials section range drifted from "
          "MaterialRegistry.h's compiled-in value for " +
              std::string(std::get<0>(compiled)));
    }
  }
  bw::core::clearMaterialDefaultOverrides();
}

}  // namespace

int main() {
  try {
    withoutAnyFileEverythingReadsTheCompiledDefault();
    anOverriddenParameterReadsFromTheFile();
    anUnknownMaterialOrParameterNameIsIgnored();
    clearRevertsEveryOverride();
    theRealGameYamlParsesAndMatchesCompiledDefaults();
    std::cout << "Material defaults file tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
