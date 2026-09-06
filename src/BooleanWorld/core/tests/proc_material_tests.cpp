#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/Emboss.h>
#include <core/ProcMaterialData.h>
#include <core/SerializationWorkData.h>
#include <core/YamlSerializer.h>

namespace {

using bw::core::ProcMaterialData;
using bw::core::SubMaterial;
using bw::core::TechniqueParameterSchema;
using bw::core::TechniqueSchema;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.0001f;
}

bool containsError(ProcMaterialData const& data, std::string const& expected) {
  for (auto const& error : data.getDeserializationErrors()) {
    if (error == expected) {
      return true;
    }
  }
  return false;
}

// Pinned-value fixture: Marble's real warp_scale/veins_scale bounds and
// default, as MaterialRegistry.h defined them before ADR-0023 retired that
// table - see common/tests/material_registry_tests.cpp's equivalent pin.
// This is the "at least one hand-authored Technique schema + Sub-material"
// fixture the ticket asks for.
ProcMaterialData buildMarbleCatalog() {
  ProcMaterialData data;
  data.program3d = "world_pbr.frag";
  data.program2d = "world_pbr_2d.frag";

  TechniqueSchema marble;
  marble.materialIndex = 0;
  marble.parameters.push_back(TechniqueParameterSchema{"warp_scale", 0.0f, 5.0f, 1.35f});
  marble.parameters.push_back(TechniqueParameterSchema{"veins_scale", 1.0f, 10.0f, 5.0f});
  data.techniqueSchemas.push_back(marble);

  SubMaterial weatheredSlate;
  weatheredSlate.id = "weathered_slate";
  weatheredSlate.displayName = "Weathered Slate";
  weatheredSlate.acousticPresetId = "builtin.acoustic.rock";
  weatheredSlate.materialIndex = 0;
  weatheredSlate.paramValues = {1.35f, 5.0f};
  weatheredSlate.baseColour = {0.18f, 0.18f, 0.20f};
  data.subMaterials.push_back(weatheredSlate);

  return data;
}

std::shared_ptr<bw::core::Serializer> yamlFrom(std::string const& text) {
  auto serializer = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromString(text));
  serializer->deserialize();
  return serializer;
}

void roundTripPreservesEveryField() {
  auto original = buildMarbleCatalog();
  bw::core::SerializationWorkData writeWorkData;

  auto writer = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toString());
  original.serialize(writer, writeWorkData);
  writer->serialize();
  auto yaml = static_cast<bw::core::YamlSerializer*>(writer.get())->getSerializedString();

  ProcMaterialData roundTripped;
  bw::core::SerializationWorkData readWorkData;
  auto reader = yamlFrom(yaml);
  require(roundTripped.deserialize(reader, readWorkData),
          "round-tripped catalog failed to deserialize: " +
              (roundTripped.getDeserializationErrors().empty()
                   ? std::string("<no error recorded>")
                   : roundTripped.getDeserializationErrors().front()));

  require(roundTripped.program3d == original.program3d, "program3d did not round-trip");
  require(roundTripped.program2d == original.program2d, "program2d did not round-trip");

  require(roundTripped.techniqueSchemas.size() == 1, "techniqueSchemas count did not round-trip");
  auto const& schema = roundTripped.techniqueSchemas[0];
  require(schema.materialIndex == 0, "TechniqueSchema materialIndex did not round-trip");
  require(schema.parameters.size() == 2, "TechniqueSchema parameter count did not round-trip");
  require(schema.parameters[0].name == "warp_scale", "TechniqueSchema parameter name did not round-trip");
  require(near(schema.parameters[0].minimum, 0.0f) && near(schema.parameters[0].maximum, 5.0f) &&
              near(schema.parameters[0].defaultValue, 1.35f),
          "TechniqueSchema parameter bounds did not round-trip");

  require(roundTripped.subMaterials.size() == 1, "subMaterials count did not round-trip");
  auto const& subMaterial = roundTripped.subMaterials[0];
  require(subMaterial.id == "weathered_slate", "SubMaterial id did not round-trip");
  require(subMaterial.displayName == "Weathered Slate", "SubMaterial displayName did not round-trip");
  require(subMaterial.acousticPresetId == "builtin.acoustic.rock",
          "SubMaterial Acoustic preset id did not round-trip");
  require(subMaterial.materialIndex == 0, "SubMaterial materialIndex did not round-trip");
  require(subMaterial.paramValues.size() == 2 && near(subMaterial.paramValues[0], 1.35f) &&
              near(subMaterial.paramValues[1], 5.0f),
          "SubMaterial paramValues did not round-trip");
  require(near(subMaterial.baseColour[0], 0.18f) && near(subMaterial.baseColour[1], 0.18f) &&
              near(subMaterial.baseColour[2], 0.20f),
          "SubMaterial baseColour did not round-trip");
}

// The pinned-value regression the ticket asks for, in the spirit of
// common/tests/material_registry_tests.cpp: a bad hand-edit to this fixture
// (or a future change to how defaults are authored) breaks a very specific,
// load-bearing assertion rather than a vague "something changed".
void pinnedMarbleValuesSurviveDeserialization() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "    - name: warp_scale\n"
      "      min: 0.0\n"
      "      max: 5.0\n"
      "      default: 1.35\n"
      "    - name: veins_scale\n"
      "      min: 1.0\n"
      "      max: 10.0\n"
      "      default: 5.0\n"
      "subMaterials:\n"
      "  - id: weathered_slate\n"
      "    name: Weathered Slate\n"
      "    materialIndex: 0\n"
      "    params: [1.35, 5.0]\n"
      "    baseColour: [0.18, 0.18, 0.20]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(data.deserialize(serializer, workData), "the pinned Marble fixture failed to deserialize");

  require(near(data.techniqueSchemas[0].parameters[0].defaultValue, 1.35f),
          "the Marble warp_scale default changed");
  require(near(data.subMaterials[0].paramValues[0], 1.35f),
          "Weathered Slate's warp_scale value changed");
  require(data.subMaterials[0].acousticPresetId.empty(),
          "a missing Acoustic preset id did not remain a valid unresolved state");
}

void outOfBoundsParameterValueIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "    - name: warp_scale\n"
      "      min: 0.0\n"
      "      max: 5.0\n"
      "      default: 1.35\n"
      "subMaterials:\n"
      "  - id: broken\n"
      "    name: Broken\n"
      "    materialIndex: 0\n"
      "    params: [99.0]\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "an out-of-bounds parameter value was accepted");
  require(containsError(data, "SubMaterial 'broken' parameter 'warp_scale' value is out of bounds."),
          "an out-of-bounds parameter value did not report the expected error");
}

void malformedParameterArrayIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "    - name: warp_scale\n"
      "      min: 0.0\n"
      "      max: 5.0\n"
      "      default: 1.35\n"
      "subMaterials:\n"
      "  - id: broken\n"
      "    name: Broken\n"
      "    materialIndex: 0\n"
      "    params: [1.0, 2.0]\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "a parameter count mismatch was accepted");
  require(containsError(data, "SubMaterial 'broken' has 2 parameter value(s), expected 1 for materialIndex 0."),
          "a parameter count mismatch did not report the expected error");
}

void subMaterialReferencingUnknownTechniqueIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas: []\n"
      "subMaterials:\n"
      "  - id: broken\n"
      "    name: Broken\n"
      "    materialIndex: 0\n"
      "    params: []\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "a SubMaterial with no matching TechniqueSchema was accepted");
  require(containsError(data, "SubMaterial 'broken' references materialIndex 0 with no TechniqueSchema."),
          "an unknown TechniqueSchema reference did not report the expected error");
}

void duplicateSubMaterialIdIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters: []\n"
      "subMaterials:\n"
      "  - id: dup\n"
      "    name: First\n"
      "    materialIndex: 0\n"
      "    params: []\n"
      "    baseColour: [0, 0, 0]\n"
      "  - id: dup\n"
      "    name: Second\n"
      "    materialIndex: 0\n"
      "    params: []\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "a duplicate SubMaterial id was accepted");
  require(containsError(data, "Duplicate SubMaterial id 'dup'."),
          "a duplicate SubMaterial id did not report the expected error");
}

void duplicateTechniqueSchemaIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters: []\n"
      "  - materialIndex: 0\n"
      "    parameters: []\n"
      "subMaterials: []\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "two TechniqueSchemas for the same materialIndex were accepted");
  require(containsError(data, "Duplicate TechniqueSchema for materialIndex 0."),
          "a duplicate TechniqueSchema did not report the expected error");
}

void tooManyTechniqueSchemaParametersIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "    - {name: p0, min: 0, max: 1, default: 0}\n"
      "    - {name: p1, min: 0, max: 1, default: 0}\n"
      "    - {name: p2, min: 0, max: 1, default: 0}\n"
      "    - {name: p3, min: 0, max: 1, default: 0}\n"
      "    - {name: p4, min: 0, max: 1, default: 0}\n"
      "    - {name: p5, min: 0, max: 1, default: 0}\n"
      "    - {name: p6, min: 0, max: 1, default: 0}\n"
      "    - {name: p7, min: 0, max: 1, default: 0}\n"
      "    - {name: p8, min: 0, max: 1, default: 0}\n"
      "subMaterials: []\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "a TechniqueSchema with too many parameters was accepted");
  require(containsError(data, "Too many TechniqueSchema parameters."),
          "too many TechniqueSchema parameters did not report the expected error");
}

void emptySubMaterialIdIsRejected() {
  std::string yaml =
      "program3d: world_pbr.frag\n"
      "program2d: world_pbr_2d.frag\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters: []\n"
      "subMaterials:\n"
      "  - id: ''\n"
      "    name: Nameless\n"
      "    materialIndex: 0\n"
      "    params: []\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData), "a SubMaterial with an empty id was accepted");
  require(containsError(data, "SubMaterial id must not be empty."),
          "an empty SubMaterial id did not report the expected error");
}

// Chip generation is authored per Sub-material and bounded independently of a
// Technique schema, with relational constraints preventing overlap.
void chipGenerationParametersRoundTripAndDefaultToDisabled() {
  auto original = buildMarbleCatalog();
  original.subMaterials[0].chip = {
      2.0f, 1.5f, 2.5f, 2.0f, 4.0f, 4.1f, 0.65f,
      0.75f, 2.75f, 0.4f};
  original.subMaterials[0].chip.types = {
      bw::core::ChipType::PrismaticNotch,
      bw::core::ChipType::MultiFacetSpall,
      bw::core::ChipType::VShapedNotch};

  bw::core::SerializationWorkData writeWorkData;
  auto writer = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toString());
  original.serialize(writer, writeWorkData);
  writer->serialize();
  auto yaml = static_cast<bw::core::YamlSerializer*>(writer.get())->getSerializedString();
  require(yaml.find("chip:") != std::string::npos &&
              yaml.find("minimumArrisLength:") != std::string::npos &&
              yaml.find("minimumCornerDistance:") != std::string::npos &&
              yaml.find("cornerProbability:") != std::string::npos &&
              yaml.find("types:") != std::string::npos &&
              yaml.find("PrismaticNotch") != std::string::npos &&
              yaml.find("chipMinimumArrisLength:") == std::string::npos &&
              yaml.find("emboss:") == std::string::npos,
          "Chip settings did not serialize independently of Emboss presets");

  ProcMaterialData roundTripped;
  bw::core::SerializationWorkData readWorkData;
  auto reader = yamlFrom(yaml);
  require(roundTripped.deserialize(reader, readWorkData),
          "catalog with Chip generation parameters failed to deserialize");
  require(roundTripped.subMaterials[0].chip == original.subMaterials[0].chip,
          "SubMaterial Chip generation parameters did not round-trip");

  std::string const withoutChip =
      "program3d: \"world_pbr.frag\"\n"
      "program2d: \"world_pbr_2d.frag\"\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "      - name: warp_scale\n"
      "        min: 0\n"
      "        max: 5\n"
      "        default: 1.35\n"
      "subMaterials:\n"
      "  - id: weathered_slate\n"
      "    name: Weathered Slate\n"
      "    materialIndex: 0\n"
      "    params: [1.35]\n"
      "    baseColour: [0, 0, 0]\n";

  ProcMaterialData legacy;
  bw::core::SerializationWorkData legacyWorkData;
  auto legacyReader = yamlFrom(withoutChip);
  require(legacy.deserialize(legacyReader, legacyWorkData),
          "a catalog without Chip fields was rejected");
  require(
      legacy.subMaterials[0].chip.probability == 0.0f &&
          legacy.subMaterials[0].chip.cornerProbability == 0.0f &&
          legacy.subMaterials[0].chip.types ==
              std::vector<bw::core::ChipType>{bw::core::ChipType::Tapered},
      "missing Chip fields did not default to disabled generation");
}

void invalidChipGenerationParametersAreRejected() {
  require(!bw::core::ChipParametersAreValid(
              {2.01f, 1.0f, 3.0f, 1.0f, 4.01f, 4.11f, 0.5f}),
          "a Chip wider than the absolute two-unit cap was accepted");
  require(!bw::core::ChipParametersAreValid(
              {2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 3.1f, 0.5f,
               3.0f, 1.0f, 0.5f}),
          "an inverted Corner Chip distance range was accepted");
  auto noTypes = bw::core::ChipGenerationParameters{};
  noTypes.types.clear();
  require(!bw::core::ChipParametersAreValid(noTypes),
          "an empty Arris Chip type list was accepted");
  auto duplicateTypes = bw::core::ChipGenerationParameters{};
  duplicateTypes.types.push_back(bw::core::ChipType::Tapered);
  require(!bw::core::ChipParametersAreValid(duplicateTypes),
          "duplicate Arris Chip types were accepted");

  std::string const yaml =
      "program3d: \"world_pbr.frag\"\n"
      "program2d: \"world_pbr_2d.frag\"\n"
      "techniqueSchemas:\n"
      "  - materialIndex: 0\n"
      "    parameters:\n"
      "      - name: warp_scale\n"
      "        min: 0\n"
      "        max: 5\n"
      "        default: 1.35\n"
      "subMaterials:\n"
      "  - id: weathered_slate\n"
      "    name: Weathered Slate\n"
      "    materialIndex: 0\n"
      "    params: [1.35]\n"
      "    baseColour: [0, 0, 0]\n"
      "    chip:\n"
      "      minimumArrisLength: 2\n"
      "      minimumDepth: 1\n"
      "      maximumDepth: 3\n"
      "      minimumReach: 1\n"
      "      maximumReach: 3\n"
      "      minimumSpacing: 3\n"
      "      probability: 0.5\n";

  ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  auto serializer = yamlFrom(yaml);
  require(!data.deserialize(serializer, workData),
          "overlapping Chip spacing was accepted");
  require(containsError(
              data, "SubMaterial Chip generation parameters are invalid."),
          "invalid Chip parameters did not report the expected error");
}

}  // namespace

int main() {
  try {
    roundTripPreservesEveryField();
    pinnedMarbleValuesSurviveDeserialization();
    outOfBoundsParameterValueIsRejected();
    malformedParameterArrayIsRejected();
    subMaterialReferencingUnknownTechniqueIsRejected();
    duplicateSubMaterialIdIsRejected();
    duplicateTechniqueSchemaIsRejected();
    tooManyTechniqueSchemaParametersIsRejected();
    emptySubMaterialIdIsRejected();
    chipGenerationParametersRoundTripAndDefaultToDisabled();
    invalidChipGenerationParametersAreRejected();
    std::cout << "ProcMaterial coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
