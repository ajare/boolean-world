#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/Defines.h>
#include <common/MaterialRegistry.h>
#include <core/EmbossingCatalogData.h>
#include <core/LayerBuildStep.h>
#include <core/ProcMaterialData.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {
using bw::core::EmbossingCatalogData;
using bw::core::ProcMaterialData;
using bw::core::Serializable;
using bw::core::SerializationWorkData;
using bw::core::Serializer;
using bw::core::World;
using bw::core::YamlSerializer;
namespace fs = std::filesystem;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.0001f;
}

std::string errors(Serializable const& value) {
  std::string result;
  for (auto const& error : value.getDeserializationErrors()) result += error + "; ";
  return result;
}

std::string readFile(fs::path const& path) {
  std::ifstream stream(path, std::ios::binary);
  require(bool(stream), "Could not open fixture: " + path.string());
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

template <typename T>
T loadFile(fs::path const& path) {
  auto reader = std::shared_ptr<Serializer>(YamlSerializer::fromFile(path.string()));
  reader->deserialize();
  SerializationWorkData workData{100.0f};
  T value;
  require(value.deserialize(reader, workData), path.string() + " failed validation: " + errors(value));
  return value;
}

template <typename T>
T roundTrip(T const& source) {
  auto writer = std::shared_ptr<Serializer>(YamlSerializer::toString());
  SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  auto text = static_cast<YamlSerializer*>(writer.get())->getSerializedString();
  auto reader = std::shared_ptr<Serializer>(YamlSerializer::fromString(text));
  reader->deserialize();
  SerializationWorkData readWorkData{100.0f};
  T result;
  require(result.deserialize(reader, readWorkData), "round-trip failed validation: " + errors(result));
  return result;
}

void generatedCatalogPreservesPinnedValuesAndRoundTrips(fs::path const& resources) {
  require(bw::common::TechniqueNames[0] == "Plain grey" &&
              bw::common::TechniqueNames[1] == "Marble",
          "compiled Technique names do not match the shifted shader indices");

  auto manifest = readFile(resources / "Resources.yaml");
  require(manifest.find("location: \"proc-materials-built-in.yaml\"") != std::string::npos &&
              manifest.find("type: \"ProcMaterial\"") != std::string::npos,
          "Resources.yaml does not declare the generated built-in ProcMaterial");

  auto catalog = loadFile<ProcMaterialData>(resources / "proc-materials-built-in.yaml");
  require(catalog.techniqueSchemas.size() == 39,
          "generated catalog does not contain all 39 Technique schemas");
  require(catalog.subMaterials.size() == 42,
          "generated catalog must contain 39 built-ins and three distinct level migrations");

  constexpr size_t expectedParameterCounts[39] = {
      0, 8, 7, 5, 5, 4, 5, 5, 5, 6, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 4, 4, 5, 4, 4, 5,
      5, 5, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2};
  for (size_t index = 0; index < 39; ++index) {
    auto const* schema = catalog.findTechniqueSchema(static_cast<uint32_t>(index));
    require(schema && schema->parameters.size() == expectedParameterCounts[index],
            "built-in Technique schema has the wrong parameter count");
  }

  auto const* oreSchema = catalog.findTechniqueSchema(9);
  require(oreSchema && oreSchema->parameters[5].name == "veinThickness" &&
              near(oreSchema->parameters[5].defaultValue, 0.24f),
          "Ore vein thickness is not exported by its Technique schema");

  auto const* marbleSchema = catalog.findTechniqueSchema(1);
  require(marbleSchema && marbleSchema->parameters.size() == 8 &&
              marbleSchema->parameters[0].name == "warp_scale" &&
              near(marbleSchema->parameters[0].defaultValue, 1.35f),
          "migrated Marble warp_scale schema does not match the compiled/override value");

  auto const find = [&](std::string const& id) -> bw::core::SubMaterial const* {
    for (auto const& subMaterial : catalog.subMaterials)
      if (subMaterial.id == id) return &subMaterial;
    return nullptr;
  };
  auto const* builtIn = find("builtin.marble");
  require(builtIn && builtIn->paramValues.size() == 8 &&
              near(builtIn->paramValues[0], 1.35f),
          "built-in Marble did not preserve its default warp_scale");
  auto const* wood2Schema = catalog.findTechniqueSchema(38);
  auto const* wood2 = find("builtin.wood2");
  require(wood2Schema && wood2Schema->parameters.size() == 2 && wood2 &&
              wood2->materialIndex == 38 && wood2->paramValues.size() == 2 &&
              near(wood2->paramValues[0], 0.65f) &&
              near(wood2->paramValues[1], 0.56f),
          "built-in Wood2 does not match its Technique schema");
  auto const* plainGreySchema = catalog.findTechniqueSchema(0);
  auto const* plainGrey = find("builtin.plain.grey");
  require(plainGreySchema && plainGreySchema->parameters.empty() && plainGrey &&
              plainGrey->materialIndex == 0 && plainGrey->paramValues.empty() &&
              near(plainGrey->baseColour[0], 0.5f) &&
              near(plainGrey->baseColour[1], 0.5f) &&
              near(plainGrey->baseColour[2], 0.5f),
          "built-in Plain grey does not match its parameterless Technique schema");
  auto const* migrated = find("migrated.marble.1");
  require(migrated && migrated->materialIndex == 1 &&
              migrated->paramValues.size() == 8 && near(migrated->paramValues[0], 1.1f) &&
              near(migrated->paramValues[2], 18.0f) && near(migrated->baseColour[2], 0.2f),
          "the hand-tuned level material combination was not preserved");

  auto reloaded = roundTrip(catalog);
  require(reloaded.techniqueSchemas.size() == 39 && reloaded.subMaterials.size() == 42,
          "generated catalog changed during round-trip");
}

void globalEmbossingCatalogPreservesBuiltInRelief(fs::path const& resources) {
  auto manifest = readFile(resources / "Resources.yaml");
  require(manifest.find("location: \"embossing-built-in.yaml\"") != std::string::npos &&
              manifest.find("type: \"EmbossingCatalog\"") != std::string::npos,
          "Resources.yaml does not declare the global Embossing catalog");

  auto catalog = loadFile<EmbossingCatalogData>(
      resources / "embossing-built-in.yaml");
  require(catalog.presets.size() == 7,
          "global catalog did not preserve every built-in relief definition");
  auto const* stone = catalog.findPreset("builtin.emboss.stone");
  auto const* brick = catalog.findPreset("builtin.emboss.brick");
  require(stone && stone->emboss.pattern == bw::core::EmbossPattern::Square &&
              near(stone->emboss.radius, 12.0f) &&
              near(stone->emboss.depth, 0.6f),
          "Stone Embossing was not preserved in its preset");
  require(brick && brick->emboss.pattern == bw::core::EmbossPattern::RunningBond &&
              near(brick->emboss.radius, 8.0f) &&
              near(brick->emboss.runningBondWidth, 40.0f),
          "Brick Embossing was not preserved in its preset");
  require(roundTrip(catalog).presets.size() == catalog.presets.size(),
          "global Embossing catalog changed during round-trip");
}

void migratedWorldReferencesThePreservedCombinationAndRoundTrips(fs::path const& resources) {
  auto worldPath = resources / "world-test-1.world.yaml";
  auto text = readFile(worldPath);
  require(text.find("materialIndex:") == std::string::npos && text.find("materialDef:") == std::string::npos,
          "world-test-1.world.yaml still contains legacy material data");
  require(text.find("floorEmbossPreset:") != std::string::npos &&
              text.find("ceilingEmbossPreset:") != std::string::npos &&
              text.find("wallEmbossPreset:") != std::string::npos,
          "world-test-1.world.yaml lacks explicit per-surface Emboss-preset references");

  size_t referenceCount = 0;
  for (size_t position = 0; (position = text.find("migrated.marble.1", position)) != std::string::npos;
       position += std::string("migrated.marble.1").size()) {
    ++referenceCount;
  }
  require(referenceCount > 0,
          "the world no longer references the preserved migrated Sub-material");

  auto world = loadFile<World>(worldPath);
  require(world.getNumPrimitives() > 0, "migrated world produced no Primitives");
  bool migratedFloor = false, migratedCeiling = false, migratedWall = false;
  for (auto const* primitive : world.getPrimitives()) {
    auto const& properties = primitive->getProperties();
    migratedFloor |= properties.floorMaterialId == "migrated.marble.1";
    migratedCeiling |= properties.ceilingMaterialId == "migrated.marble.1";
    migratedWall |= properties.wallMaterialId == "migrated.marble.1";
    require(properties.floorEmbossPresetId.empty() &&
                properties.ceilingEmbossPresetId.empty() &&
                properties.wallEmbossPresetId.empty(),
            "fixture acquired relief that its old Sub-material did not have");
  }
  require(migratedFloor && migratedCeiling && migratedWall,
          "the built World did not retain the migrated Sub-material on every surface kind");

  auto reloaded = roundTrip(world);
  require(reloaded.getNumPrimitives() == world.getNumPrimitives(),
          "migrated world changed during round-trip");
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    fs::path resources = BW_MATERIAL_MIGRATION_RESOURCE_DIR;
    generatedCatalogPreservesPinnedValuesAndRoundTrips(resources);
    globalEmbossingCatalogPreservesBuiltInRelief(resources);
    migratedWorldReferencesThePreservedCombinationAndRoundTrips(resources);
    std::cout << "ProcMaterial content migration coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
