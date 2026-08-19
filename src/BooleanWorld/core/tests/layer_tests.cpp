#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <core/Layer.h>
#include <core/RectanglePolygon.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makeRectangle(float x, float y, float w, float h) {
  auto rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rect->setSize(w, h);
  rect->setPosition(wp::Vector2(x, y));
  return rect;
}

void layerIsUsableWithoutAnyWorld() {
  bw::core::Layer layer(3, "Foreground", 100.0f, 10.0f);

  require(layer.getId() == 3, "a Layer did not report the id given at construction");
  require(layer.getName() == "Foreground", "a Layer did not report the name given at construction");
  require(layer.getNumPrimitives() == 0, "a new Layer was not empty");
  require(layer.getNumTriggerLines() == 0, "a new Layer had trigger lines before any were added");
}

void layerOwnsAddedPrimitivesAndFindsThemSpatially() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto rect = makeRectangle(0.0f, 0.0f, 10.0f, 10.0f);
  auto index = layer.addPrimitive(rect);

  require(index == 0, "the first primitive added to a Layer did not get index 0");
  require(layer.getNumPrimitives() == 1, "adding a primitive did not update the Layer's primitive count");
  require(layer.getPrimitive(0) == rect, "getPrimitive did not return the primitive that was added");

  auto candidates = layer.findPrimitives(rect->getBounds());
  require(std::find(candidates.begin(), candidates.end(), rect) != candidates.end(),
          "a spatial query on a Layer did not find a primitive it owns");
}

void removingAPrimitiveKeepsTheLookupGridConsistent() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto resident = makeRectangle(0.0f, 0.0f, 10.0f, 10.0f);
  layer.addPrimitive(resident);

  auto other = makeRectangle(20.0f, 20.0f, 10.0f, 10.0f);
  layer.addPrimitive(other);

  layer.removePrimitive(resident);

  require(layer.getNumPrimitives() == 1, "removing a primitive did not update the Layer's primitive count");

  auto candidates = layer.findPrimitives(other->getBounds());
  require(std::find(candidates.begin(), candidates.end(), other) != candidates.end(),
          "removing a primitive corrupted the Layer's lookup grid for a surviving primitive");
}

void layerOwnsAddedTriggerLinesAndFindsThemSpatially() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto triggerLine = new bw::core::WorldTriggerLine(
      {9.0f, 15.0f}, {11.0f, 15.0f});
  auto index = layer.addTriggerLine(triggerLine);

  require(index == 0, "the first trigger line added to a Layer did not get index 0");
  require(layer.getNumTriggerLines() == 1, "adding a trigger line did not update the Layer's trigger line count");
  require(layer.getTriggerLine(0) == triggerLine, "getTriggerLine did not return the trigger line that was added");

  auto candidates = layer.findTriggerLines(triggerLine->getBounds());
  require(std::find(candidates.begin(), candidates.end(), triggerLine) != candidates.end(),
          "a spatial query on a Layer did not find a trigger line it owns");
}

void copyingALayerDeepCopiesItsContent() {
  bw::core::Layer layer(5, "Source", 100.0f, 10.0f);
  layer.addPrimitive(makeRectangle(0.0f, 0.0f, 10.0f, 10.0f));
  layer.addTriggerLine(new bw::core::WorldTriggerLine({9.0f, 15.0f}, {11.0f, 15.0f}));

  bw::core::Layer copy(layer);

  require(copy.getId() == 5, "a copied Layer did not preserve its id");
  require(copy.getName() == "Source", "a copied Layer did not preserve its name");
  require(copy.getNumPrimitives() == 1, "a copied Layer did not preserve its primitives");
  require(copy.getNumTriggerLines() == 1, "a copied Layer did not preserve its trigger lines");
  require(copy.getPrimitive(0) != layer.getPrimitive(0),
          "a copied Layer shared a primitive pointer with its source instead of deep-copying");
}

}  // namespace

int main() {
  try {
    layerIsUsableWithoutAnyWorld();
    layerOwnsAddedPrimitivesAndFindsThemSpatially();
    removingAPrimitiveKeepsTheLookupGridConsistent();
    layerOwnsAddedTriggerLinesAndFindsThemSpatially();
    copyingALayerDeepCopiesItsContent();
    std::cout << "Layer owns and spatially queries its Primitives and WorldTriggerLines independently of any World\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
