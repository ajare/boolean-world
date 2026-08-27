#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/PrefabField.h>
#include <core/PrimitiveField.h>
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
  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 2, -1};
  require(field->placeSelected(layer, tile), "placement failed");
  require(field->setInstanceMode(layer, tile, bw::core::TileMode::Add),
          "fixture could not select Add mode");
  require(layer.getNumPrimitives() == 1 && layer.getPrimitive(0) != source,
          "PrefabField did not emit a clone");
  require(std::abs(layer.getPrimitive(0)->getPosition().x - 163.0f) < .001f &&
              std::abs(layer.getPrimitive(0)->getPosition().y + 32.0f) < .001f,
          "PrefabField did not position the clone at the Tile centre");
  require(layer.getOwningStepIndex(layer.getPrimitive(0)) == fieldIndex &&
              !layer.getStep(fieldIndex)->permitsDirectPrimitiveEditing(),
          "PrefabField output was directly selectable/editable");

  source->setPosition({9.0f, 0.0f});
  layer.rebuild();
  require(std::abs(layer.getPrimitive(0)->getPosition().x - 169.0f) < .001f,
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

void sizedTileInstancesAndModesSurviveSerialization() {
  bw::core::Layer source(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  source.addStep(definitions);
  auto* prefab = definitions->addPrefab("Small");
  definitions->setPrefabTileSize(prefab, bw::core::PrefabTileSize::Size32);
  auto* field = new bw::core::PrefabField;
  source.addStep(field);
  field->bind(source, definitions);
  field->setSelectedPrefab(*definitions, prefab);
  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size32, -3, 7};
  require(field->placeSelected(source, tile),
          "serialization fixture placement failed");
  require(field->setInstanceMode(source, tile, bw::core::TileMode::Add) &&
              field->rotateInstance(source, tile, true),
          "serialization fixture mode or rotation setup failed");

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
          "Layer containing a sized Prefab instance failed to deserialize");

  auto* loadedField = static_cast<bw::core::PrefabField*>(loaded.getStep(2));
  auto const* loadedInstance = loadedField->getInstance(tile);
  require(loadedInstance && loadedInstance->prefabId == prefab->getId() &&
              loadedInstance->rotation == 1 &&
              loadedInstance->mode == bw::core::TileMode::Add,
          "Prefab Tile size, coordinates, rotation, or mode did not round-trip");
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
  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 2, -1};
  require(field->placeSelected(layer, tile), "placement failed before reordering");
  field->setInstanceMode(layer, tile, bw::core::TileMode::Add);

  auto requireResolved = [&] {
    require(field->getDefinePrefabs(layer) == definitions &&
                field->getInstance(tile)->prefabId == prefab->getId() &&
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
  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 2, -1};
  require(field->placeSelected(*source, tile), "placement failed before copying");
  field->setInstanceMode(*source, tile, bw::core::TileMode::Add);

  auto copy = std::make_unique<bw::core::Layer>(*source);
  auto* copiedDefinitions = static_cast<bw::core::DefinePrefabs*>(copy->getStep(1));
  auto* copiedField = static_cast<bw::core::PrefabField*>(copy->getStep(2));
  auto* copiedPrefab = copiedDefinitions->getPrefab(0);
  require(copiedDefinitions != definitions && copiedPrefab != prefab &&
              copiedField != field && copiedField->getDefinePrefabs(*copy) == copiedDefinitions &&
              copiedField->getInstance(tile)->prefabId == copiedPrefab->getId() &&
              copy->getNumPrimitives() == 1,
          "a copied PrefabField retained a source definition or Prefab reference");

  source.reset();
  copy->rebuild();
  require(copy->getNumPrimitives() == 1,
          "a copied PrefabField depended on destroyed source Prefabs");
}

void gridsGenerateInStepLocalPhases() {
  bw::core::Layer layer(0, "test", 1024.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  layer.addStep(definitions);

  auto makePrefab = [&](char const* name, bw::core::PrefabTileSize size,
                        std::initializer_list<std::pair<float, uint8_t>> primitives) {
    auto* prefab = definitions->addPrefab(name);
    definitions->setPrefabTileSize(prefab, size);
    definitions->setSelectedPrefab(prefab);
    layer.setActiveStep(1);
    for (auto const& [x, priority] : primitives) {
      auto* primitive = rectangle(x);
      primitive->setPriority(priority);
      layer.addPrimitive(primitive);
    }
    definitions->clearSelectedPrefab();
    return prefab;
  };

  auto* large = makePrefab(
      "Large", bw::core::PrefabTileSize::Size256,
      {{0.0f, uint8_t{200}}});
  auto* medium = makePrefab(
      "Medium", bw::core::PrefabTileSize::Size128,
      {{0.0f, uint8_t{1}}});
  auto* small = makePrefab(
      "Small", bw::core::PrefabTileSize::Size64,
      {{0.0f, uint8_t{1}}});
  auto* tiny = makePrefab(
      "Tiny", bw::core::PrefabTileSize::Size32,
      {{9.0f, uint8_t{9}}, {2.0f, uint8_t{2}}});

  auto* field = new bw::core::PrefabField;
  layer.addStep(field);
  field->bind(layer, definitions);
  auto place = [&](bw::core::Prefab* prefab, bw::core::Tile tile) {
    field->setSelectedPrefab(*definitions, prefab);
    require(field->placeSelected(layer, tile), "phase fixture placement failed");
  };
  place(large, {bw::core::PrefabTileSize::Size256, 0, 0});
  place(medium, {bw::core::PrefabTileSize::Size128, 0, 0});
  place(small, {bw::core::PrefabTileSize::Size64, 0, 0});
  field->setInstanceMode(
      layer, {bw::core::PrefabTileSize::Size64, 0, 0}, bw::core::TileMode::Add);
  place(tiny, {bw::core::PrefabTileSize::Size32, 0, 0});

  require(layer.getNumPrimitives() == 7,
          "PrefabField did not emit the expected content and Replace squares");
  auto const base = uint64_t{2} << 16;
  uint64_t const expectedPriorities[]{
      base + 200,
      base + (uint64_t{1} << 8),
      base + (uint64_t{2} << 8) + 1,
      base + (uint64_t{4} << 8) + 1,
      base + (uint64_t{5} << 8),
      base + (uint64_t{6} << 8) + 2,
      base + (uint64_t{6} << 8) + 9};
  for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) {
    require(
        layer.getPrimitive(i)->getGeneratedPriority() == expectedPriorities[i],
        "PrefabField output did not use its step-local phase priority");
  }
  auto* mediumSquare = layer.getPrimitive(1);
  require(mediumSquare->getOperation() == bw::core::Primitive::Operation::Difference &&
              mediumSquare->getSize() == wp::Vector2{128.0f, 128.0f} &&
              mediumSquare->getPosition() == wp::Vector2{64.0f, 64.0f} &&
              mediumSquare->getPropertyContribution() ==
                  bw::core::Primitive::PropertyContribution::Transparent &&
              field->isHiddenGeneratedPrimitive(mediumSquare),
          "Replace did not emit an exact hidden Tile-sized Difference square");
  require(layer.getPrimitive(5)->getPosition().x == 18.0f &&
              layer.getPrimitive(6)->getPosition().x == 25.0f,
          "content phase did not preserve source-priority order");
}

void alignedTilesAndSizeMigrationAreUnambiguous() {
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  layer.addStep(definitions);
  auto* moving = definitions->addPrefab("Moving");
  auto* blocker = definitions->addPrefab("Blocker");
  definitions->setPrefabTileSize(blocker, bw::core::PrefabTileSize::Size128);
  auto* field = new bw::core::PrefabField;
  layer.addStep(field);
  field->bind(layer, definitions);

  require(field->tileAt(bw::core::PrefabTileSize::Size64, {0.0f, 0.0f}) ==
              bw::core::Tile{bw::core::PrefabTileSize::Size64, 0, 0} &&
              field->tileAt(bw::core::PrefabTileSize::Size64, {-0.01f, -64.0f}) ==
                  bw::core::Tile{bw::core::PrefabTileSize::Size64, -1, -1},
          "Tile lookup did not use shared-origin half-open grids");

  auto source = bw::core::Tile{bw::core::PrefabTileSize::Size64, 2, -1};
  auto destination = bw::core::Tile{bw::core::PrefabTileSize::Size128, 2, -1};
  field->setSelectedPrefab(*definitions, moving);
  field->placeSelected(layer, source);
  field->setSelectedPrefab(*definitions, blocker);
  field->placeSelected(layer, destination);
  require(!field->canMigratePrefabSize(
              moving->getId(), moving->getTileSize(), blocker->getTileSize()),
          "Prefab size migration did not detect a destination-grid collision");
  field->clearInstance(layer, destination);
  require(field->canMigratePrefabSize(
              moving->getId(), moving->getTileSize(), blocker->getTileSize()),
          "Prefab size migration remained blocked after clearing its destination");
  field->setSelectedPrefab(*definitions, moving);
  require(!field->hasSelectedTile(),
          "choosing a different-size palette Prefab retained Tile selection");
  field->selectTile(source);
  field->migratePrefabSize(
      moving->getId(), moving->getTileSize(), blocker->getTileSize());
  definitions->setPrefabTileSize(moving, blocker->getTileSize());
  require(!field->getInstance(source) && field->getInstance(destination) &&
              field->getInstance(destination)->mode == bw::core::TileMode::Replace &&
              field->getSelectedTile() == destination,
          "Prefab size migration lost its coordinates, mode, or selection");

  auto const largest =
      bw::core::Tile{bw::core::PrefabTileSize::Size256, 2, -1};
  field->migratePrefabSize(moving->getId(), moving->getTileSize(), largest.size);
  definitions->setPrefabTileSize(moving, largest.size);
  require(field->getInstance(largest)->mode == bw::core::TileMode::Add,
          "migrating a Prefab to 256 did not discard Replace mode");
  field->migratePrefabSize(moving->getId(), largest.size, destination.size);
  definitions->setPrefabTileSize(moving, destination.size);
  require(field->getInstance(destination)->mode == bw::core::TileMode::Replace,
          "migrating a Prefab from 256 did not default to Replace mode");
}

void orderedPrefabFieldsAndLaterStepsReceiveIncreasingPriorities() {
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  auto definitionsIndex = layer.addStep(definitions);
  auto* prefab = definitions->addPrefab("Ordered");
  definitions->setSelectedPrefab(prefab);
  layer.setActiveStep(definitionsIndex);
  auto* source = rectangle(0.0f);
  source->setPriority(255);
  layer.addPrimitive(source);
  definitions->clearSelectedPrefab();

  auto addField = [&] {
    auto* field = new bw::core::PrefabField;
    auto index = layer.addStep(field);
    field->bind(layer, definitions);
    field->setSelectedPrefab(*definitions, prefab);
    auto tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 0, 0};
    require(field->placeSelected(layer, tile), "ordered field placement failed");
    require(field->setInstanceMode(layer, tile, bw::core::TileMode::Add),
            "ordered field could not use Add mode");
    return index;
  };
  auto firstFieldIndex = addField();

  auto* middle = new bw::core::PrimitiveField;
  auto middleIndex = layer.addStep(middle);
  layer.setActiveStep(middleIndex);
  auto* middlePrimitive = rectangle(10.0f);
  middlePrimitive->setPriority(0);
  layer.addPrimitive(middlePrimitive);

  auto secondFieldIndex = addField();
  auto* tail = new bw::core::PrimitiveField;
  auto tailIndex = layer.addStep(tail);
  layer.setActiveStep(tailIndex);
  auto* tailPrimitive = rectangle(20.0f);
  tailPrimitive->setPriority(0);
  layer.addPrimitive(tailPrimitive);

  auto findStepPrimitive = [&](uint32_t stepIndex) {
    for (auto* primitive : layer.getPrimitives()) {
      if (layer.getOwningStepIndex(primitive) == stepIndex) return primitive;
    }
    return static_cast<bw::core::Primitive*>(nullptr);
  };
  auto* firstFieldPrimitive = findStepPrimitive(firstFieldIndex);
  auto* secondFieldPrimitive = findStepPrimitive(secondFieldIndex);
  require(firstFieldPrimitive && secondFieldPrimitive &&
              firstFieldPrimitive->getGeneratedPriority() <
                  middlePrimitive->getGeneratedPriority() &&
              middlePrimitive->getGeneratedPriority() <
                  secondFieldPrimitive->getGeneratedPriority() &&
              secondFieldPrimitive->getGeneratedPriority() <
                  tailPrimitive->getGeneratedPriority(),
          "LayerBuildStep order did not dominate step-local Primitive priorities");
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
  auto const tile = bw::core::Tile{bw::core::PrefabTileSize::Size64, 0, 0};
  require(field->placeSelected(layer, tile), "initial placement failed");
  require(field->setInstanceMode(layer, tile, bw::core::TileMode::Add) &&
              field->rotateInstance(layer, tile, true),
          "overwrite fixture could not set mode and rotation");
  field->setSelectedPrefab(*definitions, two);
  require(field->placeSelected(layer, tile) && field->getInstances().size() == 1 &&
              field->getInstance(tile)->prefabId == two->getId() &&
              field->getInstance(tile)->mode == bw::core::TileMode::Add &&
              field->getInstance(tile)->rotation == 0,
          "placement did not preserve mode, reset rotation, or overwrite the occupant");
  require(field->clearInstance(layer, tile) && !field->clearInstance(layer, tile),
          "clearing an occupied/empty Tile returned the wrong result");
}
}  // namespace

int main() {
  try {
    prefabFieldRegistersBindsByStableIdAndProtectsItsDefinitions();
    prefabFieldBindingSurvivesSerialization();
    sizedTileInstancesAndModesSurviveSerialization();
    referencesAreClonedPositionedAndStayLive();
    reorderingBoundStepsPreservesPrefabFieldReferences();
    copyingBoundPrefabFieldUsesCopiedDefinitionsAndPrefabs();
    gridsGenerateInStepLocalPhases();
    orderedPrefabFieldsAndLaterStepsReceiveIncreasingPriorities();
    alignedTilesAndSizeMigrationAreUnambiguous();
    overwriteAndClearUseOneOccupantPerTile();
    std::cout << "PrefabField placement and live fold tests passed\n";
    return 0;
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
