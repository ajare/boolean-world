#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/PrefabField.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/YamlSerializer.h>

namespace {
void require(bool value, char const* message) {
  if (!value) throw std::runtime_error(message);
}

bw::core::RectanglePolygon* rectangle(float x) {
  auto* result = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  result->setSize(8.0f, 8.0f);
  result->setPosition({x, 0.0f});
  return result;
}

void referencesAreClonedPositionedAndStayLive() {
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  auto defineIndex = layer.addStep(definitions);
  auto* prefab = definitions->addPrefab("Door");
  definitions->setSelectedPrefab(prefab);
  layer.setActiveStep(defineIndex);
  auto* source = rectangle(3.0f);
  layer.addPrimitive(source);
  definitions->clearSelectedPrefab();

  auto* field = new bw::core::PrefabField;
  auto fieldIndex = layer.addStep(field);
  field->bind(layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(layer, {2, -1}), "placement failed");
  require(layer.getNumPrimitives() == 1 && layer.getPrimitive(0) != source,
          "PrefabField did not emit a clone");
  require(std::abs(layer.getPrimitive(0)->getPosition().x - 131.0f) < .001f &&
              std::abs(layer.getPrimitive(0)->getPosition().y + 64.0f) < .001f,
          "PrefabField did not position the clone at the Tile centre");
  require(layer.getOwningStepIndex(layer.getPrimitive(0)) == fieldIndex &&
              !layer.getStep(fieldIndex)->permitsDirectPrimitiveEditing(),
          "PrefabField output was directly selectable/editable");

  source->setPosition({9.0f, 0.0f});
  layer.rebuild();
  require(std::abs(layer.getPrimitive(0)->getPosition().x - 137.0f) < .001f,
          "editing a Prefab did not propagate to its instance on rebuild");
}

void prefabFieldRegistersBindsByStableIdAndProtectsItsDefinitions() {
  auto const types = bw::core::LayerBuildStep::getRegisteredTypes();
  require(std::find(types.begin(), types.end(), "PrefabField") != types.end(),
          "the step Registry did not enumerate PrefabField");
  auto registered = std::unique_ptr<bw::core::LayerBuildStep>(
      bw::core::LayerBuildStep::instantiate("PrefabField"));
  require(registered->getType() == "PrefabField" && !registered->mayBeFirstStep() &&
              registered->primitivesParticipateInBuild() &&
              !registered->permitsDirectPrimitiveEditing() &&
              !registered->acceptsNewPrimitives(),
          "PrefabField did not declare its registration or editing capabilities");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* boundDefinitions = new bw::core::DefinePrefabs;
  layer.addStep(boundDefinitions);
  auto* unboundDefinitions = new bw::core::DefinePrefabs;
  layer.addStep(unboundDefinitions);
  auto* field = new bw::core::PrefabField;
  layer.addStep(field);
  require(field->getDefinePrefabsStepId() == ~0u &&
              field->getDefinePrefabs(layer) == nullptr && layer.getNumPrimitives() == 0,
          "a fresh PrefabField was not empty and unbound");

  field->bind(layer, boundDefinitions);
  auto const boundId = boundDefinitions->getId();
  require(field->getDefinePrefabsStepId() == boundId &&
              field->getDefinePrefabs(layer) == boundDefinitions,
          "PrefabField did not retain its DefinePrefabs step id binding");
  layer.moveStep(1, 2);
  require(field->getDefinePrefabs(layer) == boundDefinitions,
          "PrefabField binding followed a step index rather than its stable id");

  bw::core::Layer otherLayer(1, "other", 512.0f, 16.0f);
  auto* foreignDefinitions = new bw::core::DefinePrefabs;
  otherLayer.addStep(foreignDefinitions);
  try {
    field->bind(layer, foreignDefinitions);
    throw std::runtime_error("PrefabField bound to a DefinePrefabs step on another Layer");
  } catch (bw::core::CoreException const&) {
  }

  auto const boundIndex = layer.getNumSteps() - 2;
  try {
    layer.removeStep(boundIndex);
    throw std::runtime_error("removing a referenced DefinePrefabs step was not refused");
  } catch (bw::core::CoreException const&) {
  }
  layer.removeStep(1);
  require(layer.getNumSteps() == 3 && field->getDefinePrefabs(layer) == boundDefinitions,
          "removing an unbound DefinePrefabs step did not succeed normally");
}

void prefabFieldBindingSurvivesSerialization() {
  bw::core::Layer source(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  source.addStep(definitions);
  auto* field = new bw::core::PrefabField;
  source.addStep(field);
  field->bind(source, definitions);
  auto const definitionId = definitions->getId();

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeData;
  source.serialize(writer, writeData);
  writer->serialize();

  bw::core::Layer loaded;
  auto reader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();
  bw::core::SerializationWorkData readData;
  readData.accelGridSize = 16.0f;
  require(loaded.deserialize(reader, readData),
          "Layer containing PrefabField failed to deserialize");

  auto* loadedField = static_cast<bw::core::PrefabField*>(loaded.getStep(2));
  require(loadedField->getDefinePrefabsStepId() == definitionId &&
              loadedField->getDefinePrefabs(loaded) == loaded.getStep(1),
          "PrefabField binding did not survive serialization");
}

void reorderingBoundStepsPreservesPrefabFieldReferences() {
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  layer.addStep(definitions);
  auto* prefab = definitions->addPrefab("Door");
  definitions->setSelectedPrefab(prefab);
  layer.setActiveStep(1);
  layer.addPrimitive(rectangle(3.0f));
  definitions->clearSelectedPrefab();

  auto* field = new bw::core::PrefabField;
  layer.addStep(field);
  field->bind(layer, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(layer, {2, -1}), "placement failed before reordering");

  auto requireResolved = [&] {
    require(field->getDefinePrefabs(layer) == definitions &&
                field->getInstance({2, -1})->prefabId == prefab->getId() &&
                layer.getNumPrimitives() == 1,
            "reordering left a PrefabField reference dangling");
  };
  requireResolved();
  layer.moveStep(1, 2);
  requireResolved();
  layer.moveStep(1, 2);
  requireResolved();
}

void copyingBoundPrefabFieldUsesCopiedDefinitionsAndPrefabs() {
  auto source = std::make_unique<bw::core::Layer>(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  source->addStep(definitions);
  auto* prefab = definitions->addPrefab("Door");
  definitions->setSelectedPrefab(prefab);
  source->setActiveStep(1);
  source->addPrimitive(rectangle(3.0f));
  definitions->clearSelectedPrefab();

  auto* field = new bw::core::PrefabField;
  source->addStep(field);
  field->bind(*source, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  require(field->placeSelected(*source, {2, -1}), "placement failed before copying");

  auto copy = std::make_unique<bw::core::Layer>(*source);
  auto* copiedDefinitions = static_cast<bw::core::DefinePrefabs*>(copy->getStep(1));
  auto* copiedField = static_cast<bw::core::PrefabField*>(copy->getStep(2));
  auto* copiedPrefab = copiedDefinitions->getPrefab(0);
  require(copiedDefinitions != definitions && copiedPrefab != prefab &&
              copiedField != field && copiedField->getDefinePrefabs(*copy) == copiedDefinitions &&
              copiedField->getInstance({2, -1})->prefabId == copiedPrefab->getId() &&
              copy->getNumPrimitives() == 1,
          "a copied PrefabField retained a source definition or Prefab reference");

  source.reset();
  copy->rebuild();
  require(copy->getNumPrimitives() == 1,
          "a copied PrefabField depended on destroyed source Prefabs");
}

void overwriteAndClearUseOneOccupantPerTile() {
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  layer.addStep(definitions);
  auto* one = definitions->addPrefab("One");
  auto* two = definitions->addPrefab("Two");
  auto* field = new bw::core::PrefabField;
  layer.addStep(field);
  field->bind(layer, definitions);
  field->setSelectedPrefab(*definitions, one);
  require(field->placeSelected(layer, {0, 0}), "initial placement failed");
  field->setSelectedPrefab(*definitions, two);
  require(field->placeSelected(layer, {0, 0}) && field->getInstances().size() == 1 &&
              field->getInstance({0, 0})->prefabId == two->getId(),
          "placement did not overwrite the Tile occupant");
  require(field->clearInstance(layer, {0, 0}) && !field->clearInstance(layer, {0, 0}),
          "clearing an occupied/empty Tile returned the wrong result");
}
}  // namespace

int main() {
  try {
    prefabFieldRegistersBindsByStableIdAndProtectsItsDefinitions();
    prefabFieldBindingSurvivesSerialization();
    referencesAreClonedPositionedAndStayLive();
    reorderingBoundStepsPreservesPrefabFieldReferences();
    copyingBoundPrefabFieldUsesCopiedDefinitionsAndPrefabs();
    overwriteAndClearUseOneOccupantPerTile();
    std::cout << "PrefabField placement and live fold tests passed\n";
    return 0;
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
