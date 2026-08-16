#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <core/AnimatedProperty.h>
#include <core/InfluenceEye.h>
#include <core/PrimitivePropertySet.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/WorldUpdateData.h>
#include <core/YamlSerializer.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon,
          message + ": expected " + std::to_string(expected) +
              ", got " + std::to_string(actual));
}

void requireMaterialDefinitionEqual(bw::core::MaterialDefinition const& actual,
                                    bw::core::MaterialDefinition const& expected,
                                    std::string const& name) {
  require(actual.data.params == expected.data.params,
          name + " parameters were not copied");
  require(actual.data.baseColour == expected.data.baseColour,
          name + " base colour was not copied");
  require(actual.data.baseColourUint == expected.data.baseColourUint,
          name + " packed base colour was not copied");
}

void primitiveCopiesItsPropertySet() {
  bw::core::RectanglePolygon source(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  bw::core::PrimitivePropertySet properties;
  properties.floorZ = -12.5f;
  properties.ceilingZ = 84.25f;
  properties.floorMaterialIndex = 1;
  properties.ceilingMaterialIndex = 2;
  properties.wallMaterialIndex = 3;
  properties.floorMaterialDef.data = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, {0.1f, 0.2f, 0.3f}, 0x11223344};
  properties.ceilingMaterialDef.data = {{9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f}, {0.4f, 0.5f, 0.6f}, 0x55667788};
  properties.wallMaterialDef.data = {{17.0f, 18.0f, 19.0f, 20.0f, 21.0f, 22.0f, 23.0f, 24.0f}, {0.7f, 0.8f, 0.9f}, 0x99aabbcc};
  source.setProperties(properties);

  bw::core::RectanglePolygon copy(source);
  auto const& copiedProperties = copy.getProperties();

  requireNear(copiedProperties.floorZ, properties.floorZ, "floor height was not copied");
  requireNear(copiedProperties.ceilingZ, properties.ceilingZ, "ceiling height was not copied");
  require(copiedProperties.floorMaterialIndex == properties.floorMaterialIndex,
          "floor material index was not copied");
  require(copiedProperties.ceilingMaterialIndex == properties.ceilingMaterialIndex,
          "ceiling material index was not copied");
  require(copiedProperties.wallMaterialIndex == properties.wallMaterialIndex,
          "wall material index was not copied");
  requireMaterialDefinitionEqual(copiedProperties.floorMaterialDef, properties.floorMaterialDef,
                                 "floor material");
  requireMaterialDefinitionEqual(copiedProperties.ceilingMaterialDef, properties.ceilingMaterialDef,
                                 "ceiling material");
  requireMaterialDefinitionEqual(copiedProperties.wallMaterialDef, properties.wallMaterialDef,
                                 "wall material");
}

void animatedPropertyCopiesItsSerializedName() {
  bw::core::AnimatedProperty source("Copied animator");
  bw::core::AnimatedProperty copy(source);

  std::string const path = "value_object_copy_tests.yaml";
  auto serializer = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData workData;
  copy.serialize(serializer, workData);
  serializer->serialize();

  std::ifstream file(path);
  std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::remove(path.c_str());

  require(contents.find("Copied animator") != std::string::npos,
          "a copied animator did not preserve its serialized name");
}

void influenceEyeCopiesItsArcLength() {
  bw::core::InfluenceEye source({2.0f, -3.0f}, 45.0f, 123.0f);
  bw::core::InfluenceEye copy(source);

  requireNear(copy.getArcLength(), source.getArcLength(), "influence eye arc length was not copied");
}

void primitiveCopiesPreviousEntityInputs() {
  bw::core::RectanglePolygon source(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  source.setAnimationValues(bw::core::VertexTransformer::Key::Scale,
                            {{0.0f, 1.0f}, {1.0f, 2.0f}});
  source.setInputs({3.0f, 4.0f}, 30.0f, nullptr);

  bw::core::RectanglePolygon copy(source);
  copy.setInputs({3.0f, 4.0f}, 30.0f, nullptr);

  auto const& inputs = copy.getInputs();
  require(!inputs.playerMove,
          "a copied vertex transformer object treated an unchanged entity position as movement");
  require(!inputs.playerTurn,
          "a copied vertex transformer object treated an unchanged entity angle as a turn");
}

void worldCopiesPreviousPlayerPosition() {
  bw::core::World source(100.0f, 10.0f);
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {9.5f, 15.0f}, {10.5f, 15.0f}));

  bw::core::WorldUpdateData updateData{
      {10.0f, 10.0f}, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 0};
  source.update(0.0f, updateData, {100.0f, 100.0f});

  bw::core::World copy(source);
  updateData.entityPosition = {10.0f, 20.0f};
  copy.update(0.0f, updateData, {100.0f, 100.0f});

  require(copy.getTriggerLine(0)->getTotalTriggerCount() == 1,
          "a copied world did not check trigger lines from its previous player position");
}

}  // namespace

int main() {
  try {
    primitiveCopiesItsPropertySet();
    animatedPropertyCopiesItsSerializedName();
    influenceEyeCopiesItsArcLength();
    primitiveCopiesPreviousEntityInputs();
    worldCopiesPreviousPlayerPosition();
    std::cout << "Value-object copies preserve all members\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
