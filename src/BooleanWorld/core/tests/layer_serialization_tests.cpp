#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <core/BinarySerializer.h>
#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
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

bw::core::RectanglePolygon* makeRectangle(float x) {
  auto rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rect->setSize(10.0f, 10.0f);
  rect->setPosition({x, 0.0f});
  return rect;
}

bw::core::Layer makeSourceLayer() {
  bw::core::Layer layer(7, "Foreground", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));

  // A second, disabled step, so the round-trip has to carry a step list
  // rather than just the Primitives the Layer happens to be showing.
  auto disabled = std::make_unique<bw::core::PrimitiveField>();
  disabled->addPrimitive(makeRectangle(20.0f));
  auto disabledIndex = layer.addStep(disabled.release());
  layer.setStepEnabled(disabledIndex, false);

  layer.addTriggerLine(new bw::core::WorldTriggerLine({9.0f, 15.0f}, {11.0f, 15.0f}));

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

  require(loaded.getNumSteps() == source.getNumSteps(),
          "a Layer's build step count did not round-trip");
  for (uint32_t i = 0; i < loaded.getNumSteps(); ++i) {
    require(loaded.getStep(i)->getType() == source.getStep(i)->getType(),
            "a Layer's build step types did not round-trip in order");
    require(loaded.getStep(i)->isEnabled() == source.getStep(i)->isEnabled(),
            "a Layer's build step enabled flags did not round-trip");
  }

  // The Primitives are not in the file at all: they only exist because the
  // loaded step list was re-run (docs/adr/0014).
  require(loaded.getPrimitiveField()->getNumPrimitives() ==
              source.getPrimitiveField()->getNumPrimitives(),
          "a Layer's first step did not round-trip the Primitives embedded in it");
  require(loaded.getPrimitive(0) == loaded.getPrimitiveField()->getPrimitive(0),
          "a loaded Layer's derived Primitive was not the one its first step holds");
  require(loaded.getPrimitive(0)->getPosition().x == source.getPrimitive(0)->getPosition().x,
          "rebuilding a loaded Layer did not reproduce its source's Primitives");
}

std::string readFile(std::string const& path) {
  std::ifstream stream(path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

// The shallowest indentation at which key appears in yaml, or npos if it
// never does. Nesting is what distinguishes an array the Layer itself writes
// from one written inside a build step.
size_t shallowestIndentOfKey(std::string const& yaml, std::string const& key) {
  std::istringstream lines(yaml);
  std::string line;
  auto shallowest = std::string::npos;

  while (std::getline(lines, line)) {
    auto indent = line.find_first_not_of(" -");
    if (indent == std::string::npos) {
      continue;
    }

    if (line.compare(indent, key.size(), key) == 0) {
      shallowest = std::min(shallowest, indent);
    }
  }

  return shallowest;
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

void theLayerFileFormatCarriesStepsAndNoRawPrimitivesArray() {
  std::string const path = "layer_serialization_tests_format.layer.yaml";
  auto source = makeSourceLayer();

  {
    std::shared_ptr<bw::core::Serializer> ser(bw::core::YamlSerializer::toFile(path));
    auto workData = bw::core::SerializationWorkData{};
    source.serialize(ser, workData);
    ser->serialize();
  }

  auto const yaml = readFile(path);

  auto const stepsIndent = shallowestIndentOfKey(yaml, "steps:");
  require(stepsIndent != std::string::npos, "a serialized Layer did not write a step list");

  // Every primitive array in the file belongs to a step; the Layer itself
  // writes none, so none can sit at or above the step list's own level.
  auto const primitivesIndent = shallowestIndentOfKey(yaml, "primitives:");
  require(primitivesIndent == std::string::npos || primitivesIndent > stepsIndent,
          "a serialized Layer still carries a raw primitives array of its own");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  try {
    layerRoundTripsThroughDotLayerBinaryFile();
    layerRoundTripsThroughDotLayerYamlFile();
    theLayerFileFormatCarriesStepsAndNoRawPrimitivesArray();
    std::cout << "Layer round-trips its build steps through .layer (binary) and .layer.yaml, independently of any World\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
