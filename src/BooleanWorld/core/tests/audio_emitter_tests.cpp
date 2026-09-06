#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/AudioEmitter.h>
#include <core/BinarySerializer.h>
#include <core/DefinePrefabs.h>
#include <core/LayerBuildStep.h>
#include <core/MeshPrimitive.h>
#include <core/PrefabField.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 0.01f) {
  return std::abs(left - right) <= tolerance;
}

bw::core::ComplexPolygon rectangle(
    float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

bw::core::MeshPrimitive* makeRectangle(
    bw::core::Primitive::Operation operation,
    float left, float bottom, float right, float top) {
  return bw::core::MeshPrimitive::fromComplexPolygons(
      operation, {rectangle(left, bottom, right, top)});
}

std::shared_ptr<bw::core::ArrangementWorldData> capture(
    bw::core::World const& world) {
  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(&world);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(), 8.0f, nullptr,
      world.getWedgeGenerationParameters());
}

bw::core::AudioEmitter emitter() {
  bw::core::AudioEmitter result;
  result.offset = {3.5f, -7.25f};
  result.heightOffset = 1.5f;
  result.soundId = "ambient/drip";
  result.guid = "bfe3a09d-0fd7-491d-93c5-5d2b2a427a6a";
  result.cullRadius = 37.0f;
  return result;
}

void requireEmitter(bw::core::AudioEmitter const& actual) {
  require(actual.offset == wp::Vector2(3.5f, -7.25f) && actual.heightOffset == 1.5f &&
              actual.soundId == "ambient/drip" &&
              actual.guid == "bfe3a09d-0fd7-491d-93c5-5d2b2a427a6a" &&
              actual.cullRadius == 37.0f,
          "AudioEmitter fields did not round-trip");
}

bw::core::World sourceWorld() {
  bw::core::World world(100.0f, 10.0f);
  auto primitive = std::make_unique<bw::core::RectanglePolygon>(
      bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 1.0f);
  primitive->setAudioEmitters({emitter()});
  world.addPrimitive(primitive.release());
  return world;
}

void requireWorldEmitter(bw::core::World const& world) {
  require(world.getNumPrimitives() == 1 && world.getPrimitive(0)->getAudioEmitters().size() == 1,
          "World lost its Primitive AudioEmitter");
  requireEmitter(world.getPrimitive(0)->getAudioEmitters().front());
}

void worldsRoundTripThroughBothSerializers() {
  auto source = sourceWorld();

  auto yamlWriter = std::shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeData;
  source.serialize(yamlWriter, writeData);
  yamlWriter->serialize();
  require(yamlWriter->getSerializedString().find("audioEmitters:") != std::string::npos,
          "YAML omitted AudioEmitters");
  auto yamlReader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yamlWriter->getSerializedString()));
  yamlReader->deserialize();
  bw::core::World yamlWorld;
  bw::core::SerializationWorkData yamlReadData{10.0f};
  require(yamlWorld.deserialize(yamlReader, yamlReadData), "World AudioEmitters did not deserialize from YAML");
  requireWorldEmitter(yamlWorld);

  auto binaryWriter = std::shared_ptr<bw::core::BinarySerializer>(bw::core::BinarySerializer::toString());
  source.serialize(binaryWriter, writeData);
  binaryWriter->serialize();
  auto binaryReader = std::shared_ptr<bw::core::Serializer>(
      bw::core::BinarySerializer::fromString(binaryWriter->getSerializedString()));
  binaryReader->deserialize();
  bw::core::World binaryWorld;
  bw::core::SerializationWorkData binaryReadData{10.0f};
  require(binaryWorld.deserialize(binaryReader, binaryReadData), "World AudioEmitters did not deserialize from binary");
  requireWorldEmitter(binaryWorld);
}

void captureAppliesEachSurvivalRuleAndUsesTheWinningFloor() {
  bw::core::World world(64.0f, 8.0f);
  auto* parent = makeRectangle(
      bw::core::Primitive::Operation::Union, -10.0f, -8.0f, 10.0f, 8.0f);
  bw::core::PrimitivePropertySet parentProperties;
  parentProperties.floorZ = -10.0f;
  parentProperties.ceilingZ = 10.0f;
  parent->setProperties(parentProperties);

  auto survivor = emitter();
  survivor.guid = "survives-overpaint";
  survivor.offset = {-5.0f, 0.0f};
  survivor.heightOffset = 0.5f;
  auto noSolid = emitter();
  noSolid.guid = "no-solid-face";
  noSolid.offset = {0.0f, 0.0f};
  auto hitsCeiling = emitter();
  hitsCeiling.guid = "at-ceiling";
  hitsCeiling.offset = {5.0f, 0.0f};
  hitsCeiling.heightOffset = 20.0f;
  parent->setAudioEmitters({survivor, noSolid, hitsCeiling});
  world.addPrimitive(parent);

  auto* cut = makeRectangle(
      bw::core::Primitive::Operation::Difference, -1.0f, -1.0f, 1.0f, 1.0f);
  cut->setPriority(2);
  world.addPrimitive(cut);

  auto* overpaint = makeRectangle(
      bw::core::Primitive::Operation::Union, -8.0f, -2.0f, -3.0f, 2.0f);
  bw::core::PrimitivePropertySet winningProperties;
  winningProperties.floorZ = 5.0f;
  winningProperties.ceilingZ = 10.0f;
  overpaint->setProperties(winningProperties);
  overpaint->setPriority(3);
  world.addPrimitive(overpaint);

  auto data = capture(world);
  auto const& captured = data->getCapturedAudioEmitters();
  require(captured.size() == 1 && captured.front().guid == "survives-overpaint",
          "capture did not independently apply solid-face, parent-contribution, and ceiling rules");
  require(captured.front().position == wp::Vector2{-5.0f, 0.0f} &&
              near(captured.front().height, 5.5f) &&
              !captured.front().placementKey,
          "capture did not use the property-winning Primitive's floor or direct identity");
}

void replaceSquareClearsParentContributionAndPlacementKeyIdentifiesPrefab() {
  bw::core::World world(128.0f, 8.0f);
  auto* layer = world.getActiveLayer();
  auto* base = makeRectangle(
      bw::core::Primitive::Operation::Union, 0.0f, 0.0f, 64.0f, 64.0f);
  auto cleared = emitter();
  cleared.guid = "cleared-parent";
  cleared.offset = {16.0f, 16.0f};
  base->setAudioEmitters({cleared});
  world.addPrimitive(base);

  auto* definitions = new bw::core::DefinePrefabs;
  layer->addStep(definitions);
  auto* prefab = definitions->addPrefab("replacement");
  definitions->setSelectedPrefab(prefab);
  layer->setActiveStep(1);
  auto* replacement = makeRectangle(
      bw::core::Primitive::Operation::Union, -32.0f, -32.0f, 32.0f, 32.0f);
  auto placed = emitter();
  placed.guid = "placed-emitter";
  placed.offset = {0.0f, 0.0f};
  replacement->setAudioEmitters({placed});
  world.addPrimitive(replacement);
  definitions->clearSelectedPrefab();

  auto* field = new bw::core::PrefabField;
  layer->addStep(field);
  field->bind(*layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(
              *layer, {bw::core::PrefabTileSize::Size64, 0, 0},
              bw::core::TileMode::Replace),
          "Replace fixture could not place its Prefab");

  auto data = capture(world);
  auto const& captured = data->getCapturedAudioEmitters();
  require(captured.size() == 1 && captured.front().guid == "placed-emitter",
          "a Replace square did not clear the earlier parent emitter");
  require(captured.front().position == wp::Vector2{32.0f, 32.0f} &&
              captured.front().placementKey ==
                  bw::core::EmitterPlacementKey{0, 0, 64},
          "captured Prefab emitter lost its fixed position or placement key");
}

void captureUsesTheWedgeRaisedWinningFloor() {
  bw::core::World world(64.0f, 8.0f);
  auto* parent = makeRectangle(
      bw::core::Primitive::Operation::Union, -10.0f, -8.0f, 10.0f, 8.0f);
  bw::core::PrimitivePropertySet parentProperties;
  parentProperties.floorZ = -10.0f;
  parentProperties.ceilingZ = 20.0f;
  parent->setProperties(parentProperties);
  auto source = emitter();
  source.guid = "wedge-height";
  source.offset = {0.0f, -6.785185f};
  source.heightOffset = 0.25f;
  parent->setAudioEmitters({source});
  world.addPrimitive(parent);

  auto* winner = makeRectangle(
      bw::core::Primitive::Operation::Union, -10.0f, -8.0f, 10.0f, 8.0f);
  bw::core::PrimitivePropertySet winnerProperties;
  winnerProperties.floorZ = 5.0f;
  winnerProperties.ceilingZ = 20.0f;
  winner->setProperties(winnerProperties);
  winner->setPriority(2);
  world.addPrimitive(winner);

  bw::core::WedgeGenerationParameters wedges{
      true, 6.0f, 6.0f, 3.0f, 3.0f, 2.0f, 2.0f};
  world.setWedgeGenerationParameters(wedges);

  auto data = capture(world);
  auto const& captured = data->getCapturedAudioEmitters();
  auto position = wp::Vector2{0.0f, -6.785185f};
  require(captured.size() == 1 && data->getFloorHeight(position) > 5.0f,
          "Wedge fixture did not raise the winning face's floor");
  require(near(captured.front().height,
               data->getFloorHeight(position) + source.heightOffset),
          "capture ran before floor Wedges or used the parent's floorZ");
}

void prefabCopiesKeepEmitterGuids() {
  bw::core::Layer source(0, "Prefab source", 100.0f, 10.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  source.addStep(definitions);
  auto* prefab = definitions->addPrefab("Dripping room");
  definitions->setSelectedPrefab(prefab);
  source.setActiveStep(1);
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero, 1.0f);
  primitive->setAudioEmitters({emitter()});
  source.addPrimitive(primitive);

  bw::core::Layer copy(source);
  auto* copiedDefinitions = static_cast<bw::core::DefinePrefabs*>(copy.getStep(1));
  auto const& copiedEmitters = copiedDefinitions->getPrefab(0)->getPrimitive(0)->getAudioEmitters();
  require(copiedEmitters.size() == 1, "copied Prefab lost its AudioEmitter");
  requireEmitter(copiedEmitters.front());
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    worldsRoundTripThroughBothSerializers();
    captureAppliesEachSurvivalRuleAndUsesTheWinningFloor();
    replaceSquareClearsParentContributionAndPlacementKeyIdentifiesPrefab();
    captureUsesTheWedgeRaisedWinningFloor();
    prefabCopiesKeepEmitterGuids();
    std::cout << "AudioEmitter serialization, capture, and Prefab copying passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
