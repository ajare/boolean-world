#include <cmath>
#include <iostream>
#include <stdexcept>

#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/PrefabField.h>
#include <core/RectanglePolygon.h>

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
    referencesAreClonedPositionedAndStayLive();
    overwriteAndClearUseOneOccupantPerTile();
    std::cout << "PrefabField placement and live fold tests passed\n";
    return 0;
  } catch (std::exception const& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
