#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/AnimatedProperty.h>
#include <core/DynamicWorldDataGenerator.h>
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
  require(actual.data.packedColour() == expected.data.packedColour(),
          name + " packed base colour differs");
}

void primitivesInitializeMaterialIndices() {
  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  auto const& properties = primitive.getProperties();

  require(properties.floorMaterialIndex == 0,
          "a new primitive did not initialize its floor material index");
  require(properties.ceilingMaterialIndex == 0,
          "a new primitive did not initialize its ceiling material index");
  require(properties.wallMaterialIndex == 0,
          "a new primitive did not initialize its wall material index");
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
  properties.floorMaterialDef.data = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, {0.1f, 0.2f, 0.3f}};
  properties.ceilingMaterialDef.data = {{9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f}, {0.4f, 0.5f, 0.6f}};
  properties.wallMaterialDef.data = {{17.0f, 18.0f, 19.0f, 20.0f, 21.0f, 22.0f, 23.0f, 24.0f}, {0.7f, 0.8f, 0.9f}};
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
  {
    auto mutation = source.mutate();
    mutation.animation(bw::core::VertexTransformer::Key::Scale)
        .setPoints({{0.0f, 1.0f}, {1.0f, 2.0f}});
  }
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
      {10.0f, 10.0f}, 0.0f, 0.0f, 0.0f, 0.0f, false, false, bw::core::SelectLayer(0)};
  source.update(0.0f, updateData, {100.0f, 100.0f});

  bw::core::World copy(source);
  updateData.entityPosition = {10.0f, 20.0f};
  copy.update(0.0f, updateData, {100.0f, 100.0f});

  require(copy.getTriggerLine(0)->getTotalTriggerCount() == 1,
          "a copied world did not check trigger lines from its previous player position");
}

void copiedWorldRemainsSelfContainedAfterSourceDestruction() {
  auto source = std::make_unique<bw::core::World>(500.0f, 10.0f);
  source->setAlwaysUpdateVertices(true);

  auto* triggerLine = new bw::core::WorldTriggerLine(
      7, {30.0f, -5.0f}, {30.0f, 5.0f},
      bw::core::WorldTriggerLineSide::Both);
  require(triggerLine->checkCollide({29.0f, 0.0f}, {31.0f, 0.0f}, 0.0f),
          "source trigger line did not record its authored test crossing");
  source->addTriggerLine(triggerLine);

  auto makePrimitive = [] {
    auto primitive = std::make_unique<bw::core::RectanglePolygon>(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero,
        1.0f);
    primitive->setLayer(3);
    primitive->setSize(6.0f, 6.0f);
    return primitive;
  };

  auto root = makePrimitive();
  auto child = makePrimitive();
  auto grandchild = makePrimitive();
  root->setPosition({0.0f, 0.0f});
  child->setPosition({10.0f, 0.0f});
  grandchild->setPosition({10.0f, 0.0f});
  root->setPriority(0);
  child->setPriority(1);
  grandchild->setPriority(2);

  bw::core::PrimitivePropertySet properties;
  properties.floorZ = 12.5f;
  properties.ceilingZ = 63.0f;
  properties.floorMaterialIndex = 4;
  properties.ceilingMaterialIndex = 5;
  properties.wallMaterialIndex = 6;
  grandchild->setProperties(properties);

  auto* rootPtr = root.get();
  auto* childPtr = child.get();
  source->addPrimitive(root.release());
  source->addPrimitive(child.release());
  source->addPrimitive(grandchild.release());
  childPtr->setParent(rootPtr);
  source->getPrimitive(2)->setParent(childPtr);

  auto* sourceGenerator = new bw::core::DynamicWorldDataGenerator(source.get());
  bw::core::LayerSelection selectedLayers;
  selectedLayers.set(3);
  selectedLayers.set(7);
  sourceGenerator->setLayerSelection(selectedLayers);
  sourceGenerator->setAlwaysUpdateVertices(true);
  sourceGenerator->setAllowCommitIfVisible(true);
  sourceGenerator->setScheduledGenerationInterval(2.5f);
  source->setWorldDataGenerator(sourceGenerator);
  bw::core::WorldUpdateData sourceUpdateData{
      {0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, false, false, selectedLayers};
  source->update(0.0f, sourceUpdateData, {100.0f, 100.0f});
  require(source->getWorldData()->getContainingFaceIndex({20.0f, 0.0f}) != ~0u,
          "source dynamic generator did not establish clipping state to copy");

  auto copy = std::make_unique<bw::core::World>(*source);
  require(copy->getPrimitive(0) != rootPtr &&
              copy->getPrimitive(1) != childPtr,
          "world copy retained source primitive instances");
  require(copy->getTriggerLine(0) != triggerLine &&
              copy->getTriggerLine(0)->getLayer() == 7 &&
              copy->getTriggerLine(0)->getPoint(0) == wp::Vector2(30.0f, -5.0f) &&
              copy->getTriggerLine(0)->getPoint(1) == wp::Vector2(30.0f, 5.0f) &&
              copy->getTriggerLine(0)->getSide() == bw::core::WorldTriggerLineSide::Both &&
              copy->getTriggerLine(0)->getTotalTriggerCount() == 1,
          "world copy lost trigger-line values");
  require(copy->getAlwaysUpdateVertices(),
          "world copy lost its vertex-update setting");

  auto* copiedGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      copy->getWorldDataGenerator());
  require(copiedGenerator &&
              copiedGenerator->getLayerSelection() == selectedLayers &&
              copiedGenerator->getAlwaysUpdateVertices() &&
              copiedGenerator->getAllowCommitIfVisible() &&
              copiedGenerator->getScheduledGenerationInterval() == 2.5f,
          "world copy lost selected layers or dynamic generator settings");
  auto const copiedSourcePrimitives = copiedGenerator->getSourceClippingPrimitives();
  auto const copiedActivePrimitives = copiedGenerator->getActiveClippingPrimitives();
  require(copiedSourcePrimitives.size() == copy->getNumPrimitives() &&
              copiedActivePrimitives.size() == copy->getNumPrimitives(),
          "copied dynamic generator lost its clipping primitive set");
  for (uint32_t i = 0; i < copy->getNumPrimitives(); ++i) {
    require(copiedSourcePrimitives[i].id == i &&
                copiedActivePrimitives[i].id == i,
            "copied dynamic generator lost primitive snapshot identities");
  }
  auto const copiedProperties = copy->getPrimitive(2)->getProperties();
  require(copiedProperties.floorZ == properties.floorZ &&
              copiedProperties.ceilingZ == properties.ceilingZ &&
              copiedProperties.floorMaterialIndex == properties.floorMaterialIndex &&
              copiedProperties.ceilingMaterialIndex == properties.ceilingMaterialIndex &&
              copiedProperties.wallMaterialIndex == properties.wallMaterialIndex,
          "world copy lost primitive properties");

  auto const oldGrandchildVertex =
      copy->getPrimitive(2)->getVertices()[0][0][0].p;
  source.reset();

  copy->getPrimitive(0)->setPosition({5.0f, 4.0f});
  bw::core::WorldUpdateData updateData{
      {0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f, false, false, selectedLayers};
  copy->update(0.0f, updateData, {100.0f, 100.0f});
  require(copy->getPrimitive(2)->getVertices()[0][0][0].p ==
              oldGrandchildVertex + wp::Vector2(5.0f, 4.0f),
          "copied parent chain still used source primitives");

  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  copy->serialize(serializer, workData);
  serializer->serialize();
  auto const yaml = serializer->getSerializedString();
  require(yaml.find("parentId: 0") != std::string::npos &&
              yaml.find("parentId: 1") != std::string::npos,
          "copied parent chain could not be serialized after source destruction");

  auto const& grandchildContour = copy->getPrimitive(2)->getVertices()[0][0];
  wp::Vector2 grandchildInterior = wp::Vector2::ZERO;
  for (auto const& vertex : grandchildContour) {
    grandchildInterior += vertex.p;
  }
  grandchildInterior /= float(grandchildContour.size());

  copiedGenerator->generateBlocking();
  auto const worldData = copy->getWorldData();
  auto const& arrangement = worldData->getArrangement();
  auto const faceIndex = worldData->getContainingFaceIndex(grandchildInterior);
  require(faceIndex != ~0u,
          "copied dynamic generator could not generate after source destruction at " +
              std::to_string(grandchildInterior.x) + ", " +
              std::to_string(grandchildInterior.y) + " (" +
              std::to_string(arrangement.faces.size()) + " faces, " +
              std::to_string(copiedGenerator->getNumCommits()) + " commits)");
  require(arrangement.palette[arrangement.faces[faceIndex].paletteIndex].floorZ ==
              properties.floorZ,
          "copied dynamic generation lost primitive properties");
}

}  // namespace

int main() {
  try {
    primitivesInitializeMaterialIndices();
    primitiveCopiesItsPropertySet();
    animatedPropertyCopiesItsSerializedName();
    influenceEyeCopiesItsArcLength();
    primitiveCopiesPreviousEntityInputs();
    worldCopiesPreviousPlayerPosition();
    copiedWorldRemainsSelfContainedAfterSourceDestruction();
    std::cout << "Value-object copies preserve all members\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
