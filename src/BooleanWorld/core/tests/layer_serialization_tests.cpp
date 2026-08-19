#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/BinarySerializer.h>
#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/WorldTriggerLine.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::Layer makeSourceLayer() {
  bw::core::Layer layer(7, "Foreground", 100.0f, 10.0f);

  auto rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rect->setSize(10.0f, 10.0f);
  layer.addPrimitive(rect);

  layer.addTriggerLine(new bw::core::WorldTriggerLine(0, {9.0f, 15.0f}, {11.0f, 15.0f}));

  return layer;
}

void checkRoundTrippedLayer(bw::core::Layer const& source, bw::core::Layer const& loaded) {
  require(loaded.getId() == source.getId(), "a Layer's id did not round-trip");
  require(loaded.getName() == source.getName(), "a Layer's name did not round-trip");
  require(loaded.getNumPrimitives() == source.getNumPrimitives(),
          "a Layer's primitive count did not round-trip");
  require(loaded.getNumTriggerLines() == source.getNumTriggerLines(),
          "a Layer's trigger line count did not round-trip");
  require(loaded.getPrimitive(0)->getType() == source.getPrimitive(0)->getType(),
          "a Layer's primitive type did not round-trip");
}

void layerRoundTripsThroughDotLayerBinaryFile() {
  std::string const path = "layer_serialization_tests.layer";
  auto source = makeSourceLayer();

  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::BinarySerializer::toFile(path));
    auto workData = bw::core::SerializationWorkData{};
    source.serialize(ser, workData);
    ser->serialize();
  }

  bw::core::Layer loaded;
  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::BinarySerializer::fromFile(path));
    ser->deserialize();
    auto workData = bw::core::SerializationWorkData{};
    workData.accelGridSize = 10.0f;
    require(loaded.deserialize(ser, workData),
            "a Layer failed to deserialize from a .layer binary file");
  }

  checkRoundTrippedLayer(source, loaded);

  std::remove(path.c_str());
}

void layerRoundTripsThroughDotLayerYamlFile() {
  std::string const path = "layer_serialization_tests.layer.yaml";
  auto source = makeSourceLayer();

  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::YamlSerializer::toFile(path));
    auto workData = bw::core::SerializationWorkData{};
    source.serialize(ser, workData);
    ser->serialize();
  }

  bw::core::Layer loaded;
  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::YamlSerializer::fromFile(path));
    ser->deserialize();
    auto workData = bw::core::SerializationWorkData{};
    workData.accelGridSize = 10.0f;
    require(loaded.deserialize(ser, workData),
            "a Layer failed to deserialize from a .layer.yaml file");
  }

  checkRoundTrippedLayer(source, loaded);

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    layerRoundTripsThroughDotLayerBinaryFile();
    layerRoundTripsThroughDotLayerYamlFile();
    std::cout << "Layer round-trips through .layer (binary) and .layer.yaml, independently of any World\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
