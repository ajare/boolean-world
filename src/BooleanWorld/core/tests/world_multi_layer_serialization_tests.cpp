#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/BinarySerializer.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/WorldDataGenerator.h>
#include <core/WorldTriggerLine.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makeRectangle(float x) {
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setPosition({x, 0.0f});
  primitive->setSize(2.0f, 2.0f);
  return primitive;
}

bw::core::World buildThreeLayerWorld() {
  bw::core::World world(100.0f, 10.0f);
  world.getActiveLayer()->setName("Base");
  world.addPrimitive(makeRectangle(0.0f));
  world.addTriggerLine(new bw::core::WorldTriggerLine({-1.0f, 5.0f}, {1.0f, 5.0f}));

  auto* second = world.addLayer("Second");
  second->addPrimitive(makeRectangle(10.0f));
  second->addPrimitive(makeRectangle(20.0f));

  auto* third = world.addLayer("Third");
  third->addTriggerLine(new bw::core::WorldTriggerLine({-1.0f, -5.0f}, {1.0f, -5.0f}));

  return world;
}

void requireMatchesThreeLayerWorld(bw::core::World const& world) {
  require(world.getNumLayers() == 3, "a multi-Layer World lost or gained Layers across a round-trip");

  auto const& layers = world.getLayers();

  require(layers[0]->getId() == 0 && layers[0]->getName() == "Base",
          "the first Layer did not keep its id and name across a round-trip");
  require(layers[0]->getNumPrimitives() == 1 && layers[0]->getNumTriggerLines() == 1,
          "the first Layer did not keep its Primitives and WorldTriggerLines across a round-trip");

  require(layers[1]->getId() == 1 && layers[1]->getName() == "Second",
          "the second Layer did not keep its id and name across a round-trip");
  require(layers[1]->getNumPrimitives() == 2,
          "the second Layer did not keep every Primitive across a round-trip");
  require(layers[1]->getPrimitive(0)->getPosition() == wp::Vector2(10.0f, 0.0f) &&
              layers[1]->getPrimitive(1)->getPosition() == wp::Vector2(20.0f, 0.0f),
          "the second Layer's Primitives did not keep their content across a round-trip");

  require(layers[2]->getId() == 2 && layers[2]->getName() == "Third",
          "the third Layer did not keep its id and name across a round-trip");
  require(layers[2]->getNumTriggerLines() == 1,
          "the third Layer did not keep its WorldTriggerLine across a round-trip");

  require(world.getActiveLayerIndex() == 0,
          "the active Layer index was not reset to 0 by deserialization");
  require(world.getWorldDataGenerator()->getLayerSelection() ==
              bw::core::SelectLayer(layers[0]->getId()),
          "the generation layer-selection mask was not reset to just the active Layer");
}

void aMultiLayerWorldRoundTripsThroughYaml() {
  auto source = buildThreeLayerWorld();

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();

  bw::core::World target;
  bw::core::SerializationWorkData readWorkData{10.0f};
  require(target.deserialize(reader, readWorkData),
          "a multi-Layer World failed to deserialize from YAML");

  requireMatchesThreeLayerWorld(target);
}

void aMultiLayerWorldRoundTripsThroughBinary() {
  std::string const path = "world_multi_layer_serialization_tests.world";
  auto source = buildThreeLayerWorld();

  {
    auto writer = std::shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::toFile(path));
    bw::core::SerializationWorkData writeWorkData;
    source.serialize(writer, writeWorkData);
    writer->serialize();
  }

  bw::core::World target;
  {
    auto reader = std::shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(path));
    reader->deserialize();
    bw::core::SerializationWorkData readWorkData{10.0f};
    require(target.deserialize(reader, readWorkData),
            "a multi-Layer World failed to deserialize from a .world binary file");
  }

  requireMatchesThreeLayerWorld(target);

  std::remove(path.c_str());
}

void loadingResetsTheActiveLayerIndexEvenWhenTheActiveLayerWasReordered() {
  auto source = buildThreeLayerWorld();

  // Move the active Layer ("Base") away from index 0 before saving, so a
  // load that merely happened to leave the index untouched wouldn't pass
  // this by accident.
  source.moveLayer(0, 2);
  require(source.getActiveLayerIndex() == 2,
          "the test did not establish a non-zero active Layer index to be reset by loading");

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();

  bw::core::World target;
  bw::core::SerializationWorkData readWorkData{10.0f};
  require(target.deserialize(reader, readWorkData),
          "a reordered multi-Layer World failed to deserialize from YAML");

  require(target.getNumLayers() == 3,
          "a reordered multi-Layer World lost or gained Layers across a round-trip");
  require(target.getActiveLayerIndex() == 0,
          "the active Layer index was not reset to 0 after loading a reordered World");
  require(target.getWorldDataGenerator()->getLayerSelection() ==
              bw::core::SelectLayer(target.getLayers()[0]->getId()),
          "the generation layer-selection mask was not reset to whichever Layer now sits at index 0");
}

void theActiveLayerIndexAndLayerSelectionMaskAreAbsentFromBothSerializedForms() {
  auto source = buildThreeLayerWorld();

  auto yamlWriter = std::shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData yamlWorkData;
  source.serialize(yamlWriter, yamlWorkData);
  yamlWriter->serialize();

  auto const& yaml = yamlWriter->getSerializedString();
  require(yaml.find("activeLayer") == std::string::npos,
          "the active Layer index leaked into the serialized YAML form");
  require(yaml.find("layerSelection") == std::string::npos,
          "the generation layer-selection mask leaked into the serialized YAML form");
}

}  // namespace

int main() {
  try {
    aMultiLayerWorldRoundTripsThroughYaml();
    aMultiLayerWorldRoundTripsThroughBinary();
    loadingResetsTheActiveLayerIndexEvenWhenTheActiveLayerWasReordered();
    theActiveLayerIndexAndLayerSelectionMaskAreAbsentFromBothSerializedForms();
    std::cout << "A multi-Layer World round-trips through both serializers\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
