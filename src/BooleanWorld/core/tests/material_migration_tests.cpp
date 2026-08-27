#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/ProcMaterialData.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {
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
  auto manifest = readFile(resources / "Resources.yaml");
  require(manifest.find("location: \"proc-materials-built-in.yaml\"") != std::string::npos &&
              manifest.find("type: \"ProcMaterial\"") != std::string::npos,
          "Resources.yaml does not declare the generated built-in ProcMaterial");

  auto catalog = loadFile<ProcMaterialData>(resources / "proc-materials-built-in.yaml");
  require(catalog.techniqueSchemas.size() == 38,
          "generated catalog does not contain all 38 Technique schemas");
  require(catalog.subMaterials.size() == 41,
          "generated catalog must contain 38 built-ins and three distinct level migrations");

  auto const* marbleSchema = catalog.findTechniqueSchema(0);
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
  auto const* wood2Schema = catalog.findTechniqueSchema(37);
  auto const* wood2 = find("builtin.wood2");
  require(wood2Schema && wood2Schema->parameters.size() == 1 && wood2 &&
              wood2->materialIndex == 37 && wood2->paramValues.size() == 1 &&
              near(wood2->paramValues[0], 0.65f),
          "built-in Wood2 does not match its Technique schema");
  auto const* migrated = find("migrated.marble.1");
  require(migrated && migrated->paramValues.size() == 8 && near(migrated->paramValues[0], 1.1f) &&
              near(migrated->paramValues[2], 18.0f) && near(migrated->baseColour[2], 0.2f),
          "the hand-tuned level material combination was not preserved");

  auto reloaded = roundTrip(catalog);
  require(reloaded.techniqueSchemas.size() == 38 && reloaded.subMaterials.size() == 41,
          "generated catalog changed during round-trip");
}

void migratedWorldReferencesThePreservedCombinationAndRoundTrips(fs::path const& resources) {
  auto worldPath = resources / "world-test-1.yaml";
  auto text = readFile(worldPath);
  require(text.find("materialIndex:") == std::string::npos && text.find("materialDef:") == std::string::npos,
          "world-test-1.yaml still contains legacy material data");

  size_t referenceCount = 0;
  for (size_t position = 0; (position = text.find("migrated.marble.1", position)) != std::string::npos;
       position += std::string("migrated.marble.1").size()) {
    ++referenceCount;
  }
  require(referenceCount == 9, "not every authored floor/ceiling/wall field was migrated");

  auto world = loadFile<World>(worldPath);
  require(world.getNumPrimitives() > 0, "migrated world produced no Primitives");
  size_t referencedPrimitiveCount = 0;
  for (auto const* primitive : world.getPrimitives()) {
    auto const& properties = primitive->getProperties();
    // PrefabField's generated Replace squares are not authored level
    // surfaces and intentionally retain empty material ids.
    if (properties.floorMaterialId.empty() && properties.ceilingMaterialId.empty() &&
        properties.wallMaterialId.empty())
      continue;
    ++referencedPrimitiveCount;
    require(properties.floorMaterialId == "migrated.marble.1" &&
                properties.ceilingMaterialId == "migrated.marble.1" &&
                properties.wallMaterialId == "migrated.marble.1",
            "a built authored Primitive did not resolve to the migrated Sub-material id");
  }
  require(referencedPrimitiveCount > 0, "no built Primitive retained the migrated Sub-material id");

  auto reloaded = roundTrip(world);
  require(reloaded.getNumPrimitives() == world.getNumPrimitives(),
          "migrated world changed during round-trip");
}
}  // namespace

int main() {
  try {
    fs::path resources = BW_MATERIAL_MIGRATION_RESOURCE_DIR;
    generatedCatalogPreservesPinnedValuesAndRoundTrips(resources);
    migratedWorldReferencesThePreservedCombinationAndRoundTrips(resources);
    std::cout << "ProcMaterial content migration coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
