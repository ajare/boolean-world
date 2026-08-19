#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/CoreException.h>
#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireCoreException(Function&& function, char const* message) {
  try {
    function();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

bw::core::RectanglePolygon* makeRectangle(float x) {
  auto* rect = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rect->setSize(10.0f, 10.0f);
  rect->setPosition(wp::Vector2(x, 0.0f));
  return rect;
}

// A step is only ever handed to a Layer, which owns it from then on, so every
// helper here returns a raw pointer the Layer adopts.
bw::core::PrimitiveField* makeField(std::vector<float> const& positions) {
  auto field = std::make_unique<bw::core::PrimitiveField>();
  for (auto x : positions) {
    field->addPrimitive(makeRectangle(x));
  }
  return field.release();
}

std::vector<float> builtPositions(bw::core::Layer const& layer) {
  std::vector<float> positions;
  for (auto const* primitive : layer.getPrimitives()) {
    positions.push_back(primitive->getPosition().x);
  }
  return positions;
}

void aNewLayerStartsWithOneEmptyPrimitiveFieldStep() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  require(layer.getNumSteps() == 1, "a new Layer did not start with exactly one build step");
  require(layer.getStep(0) == layer.getPrimitiveField(),
          "a new Layer's first step was not its PrimitiveField step");
  require(layer.getStep(0)->getType() == "PrimitiveField",
          "a new Layer's first step was not of type PrimitiveField");
  require(layer.getStep(0)->isEnabled(), "a new Layer's first step was not enabled");
  require(layer.getPrimitiveField()->getNumPrimitives() == 0,
          "a new Layer's PrimitiveField step was not empty");
  require(layer.getNumPrimitives() == 0, "a new Layer produced Primitives from an empty step list");
}

void executingAPrimitiveFieldStepAddsItsEmbeddedPrimitives() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* embedded = makeRectangle(10.0f);
  layer.getPrimitiveField()->addPrimitive(embedded);

  require(layer.getNumPrimitives() == 0,
          "a Layer produced a Primitive before it was rebuilt");

  layer.rebuild();

  require(layer.getNumPrimitives() == 1,
          "rebuilding did not add a PrimitiveField step's embedded Primitive");
  require(layer.getPrimitive(0) == embedded,
          "a PrimitiveField step's execute() did not add its own Primitive verbatim");
}

void authoringThroughTheLayerFacadeGoesIntoTheFirstStep() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* authored = makeRectangle(10.0f);
  auto index = layer.addPrimitive(authored);

  require(index == 0, "the first Primitive authored on a Layer did not get index 0");
  require(layer.getPrimitive(0) == authored,
          "an authored Primitive did not appear in the Layer's derived collection");
  require(layer.getPrimitiveField()->getNumPrimitives() == 1,
          "an authored Primitive was not recorded in the Layer's first step");
  require(layer.getPrimitiveField()->getPrimitive(0) == authored,
          "an authored Primitive was not traceable to the Layer's first step");

  layer.removePrimitive(uint32_t(0));

  require(layer.getNumPrimitives() == 0, "removing an authored Primitive left it in the Layer");
  require(layer.getPrimitiveField()->getNumPrimitives() == 0,
          "removing an authored Primitive left it in the Layer's first step");
}

void multipleEnabledStepsProduceTheirConcatenatedResultInOrder() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({10.0f, 20.0f}));
  layer.addStep(makeField({30.0f}));

  require(layer.getNumSteps() == 3, "adding two steps to a Layer did not leave it with three");
  require(builtPositions(layer) == std::vector<float>({0.0f, 10.0f, 20.0f, 30.0f}),
          "a Layer's steps did not produce their concatenated result in step order");

  for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) {
    require(layer.getPrimitive(i)->getId() == i,
            "a rebuilt Layer did not renumber its derived Primitives from zero");
  }
}

void disablingAStepDropsItsPrimitivesUntilItIsEnabledAgain() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  auto middle = layer.addStep(makeField({10.0f, 20.0f}));
  layer.addStep(makeField({30.0f}));

  layer.setStepEnabled(middle, false);

  require(!layer.getStep(middle)->isEnabled(), "disabling a step did not clear its enabled flag");
  require(builtPositions(layer) == std::vector<float>({0.0f, 30.0f}),
          "a disabled step's Primitives were still present after a rebuild");

  layer.setStepEnabled(middle, true);

  require(builtPositions(layer) == std::vector<float>({0.0f, 10.0f, 20.0f, 30.0f}),
          "re-enabling a step did not bring its Primitives back in order");
}

void disablingTheFirstStepIsAllowedButDeletingAndRetypingItIsNot() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({10.0f}));

  layer.setStepEnabled(0, false);

  require(builtPositions(layer) == std::vector<float>({10.0f}),
          "disabling a Layer's first step did not drop its Primitives");
  require(layer.getPrimitiveField()->getNumPrimitives() == 1,
          "disabling a Layer's first step discarded the Primitives it holds");

  requireCoreException(
      [&] { layer.removeStep(0); },
      "a Layer allowed its first step to be deleted");
  require(layer.getStep(0)->getType() == "PrimitiveField",
          "a Layer's first step stopped being a PrimitiveField step");

  // A step's type is fixed for its lifetime, so retyping the first step could
  // only ever mean replacing it - which insertion at index 0 would have to
  // allow, and does not. A rejected step is never adopted, so the test keeps
  // ownership of it.
  auto rejected = std::unique_ptr<bw::core::LayerBuildStep>(makeField({20.0f}));
  requireCoreException(
      [&] { layer.insertStep(0, rejected.get()); },
      "a Layer allowed a step to be inserted at index 0");

  layer.setStepEnabled(0, true);

  require(builtPositions(layer) == std::vector<float>({0.0f, 10.0f}),
          "re-enabling a Layer's first step did not restore its Primitives");
}

void stepsAreOnlyInsertableAtIndexOneOrAbove() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({30.0f}));

  auto inserted = layer.insertStep(1, makeField({10.0f}));

  require(inserted == 1, "inserting a step at index 1 did not report index 1");
  require(builtPositions(layer) == std::vector<float>({0.0f, 10.0f, 30.0f}),
          "a step inserted at index 1 did not run between the first and last steps");

  auto rejected = std::unique_ptr<bw::core::LayerBuildStep>(makeField({40.0f}));
  requireCoreException(
      [&] { layer.insertStep(layer.getNumSteps() + 1, rejected.get()); },
      "a Layer accepted a step inserted past the end of its step list");
}

void removingAStepDropsThePrimitivesItProduced() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  auto added = layer.addStep(makeField({10.0f, 20.0f}));

  layer.removeStep(added);

  require(layer.getNumSteps() == 1, "removing a step did not shorten the Layer's step list");
  require(builtPositions(layer) == std::vector<float>({0.0f}),
          "removing a step left the Primitives it produced behind");
}

void copyingALayerCopiesItsStepsAndRebuildsFromThem() {
  bw::core::Layer layer(5, "Source", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({10.0f}));
  auto disabled = layer.addStep(makeField({20.0f}));
  layer.setStepEnabled(disabled, false);

  bw::core::Layer copy(layer);

  require(copy.getNumSteps() == 3, "a copied Layer did not preserve its step list");
  require(!copy.getStep(disabled)->isEnabled(),
          "a copied Layer did not preserve a step's disabled flag");
  require(builtPositions(copy) == std::vector<float>({0.0f, 10.0f}),
          "a copied Layer did not rebuild the same Primitives as its source");
  require(copy.getPrimitive(0) != layer.getPrimitive(0),
          "a copied Layer shared a Primitive pointer with its source instead of deep-copying");
  require(copy.getPrimitiveField()->getPrimitive(0) == copy.getPrimitive(0),
          "a copied Layer's derived Primitive was not the one its own first step holds");
}

}  // namespace

int main() {
  try {
    aNewLayerStartsWithOneEmptyPrimitiveFieldStep();
    executingAPrimitiveFieldStepAddsItsEmbeddedPrimitives();
    authoringThroughTheLayerFacadeGoesIntoTheFirstStep();
    multipleEnabledStepsProduceTheirConcatenatedResultInOrder();
    disablingAStepDropsItsPrimitivesUntilItIsEnabledAgain();
    disablingTheFirstStepIsAllowedButDeletingAndRetypingItIsNot();
    stepsAreOnlyInsertableAtIndexOneOrAbove();
    removingAStepDropsThePrimitivesItProduced();
    copyingALayerCopiesItsStepsAndRebuildsFromThem();
    std::cout << "A Layer derives its Primitives by running its enabled LayerBuildSteps in order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
