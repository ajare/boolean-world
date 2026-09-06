#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/AudioEmitter.h>
#include <core/BinarySerializer.h>
#include <core/DefinePrefabs.h>
#include <core/LayerBuildStep.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
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
    prefabCopiesKeepEmitterGuids();
    std::cout << "AudioEmitter serialization and Prefab copying passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
