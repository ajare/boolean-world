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

class RefusingStep final : public bw::core::LayerBuildStep {
  bw::core::Primitive* mPrimitive;
  bool mParticipatesInBuild;

public:
  explicit RefusingStep(
      bw::core::Primitive* primitive = nullptr,
      bool participatesInBuild = true)
      : mPrimitive(primitive), mParticipatesInBuild(participatesInBuild) {
  }

  ~RefusingStep() override {
    delete mPrimitive;
  }

  std::string getType() const override {
    return "RefusingStep";
  }

  bool mayBeFirstStep() const override {
    return false;
  }

  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*, bw::core::VertexTransformerObject*>& primitiveMap) const override {
    auto* primitive = mPrimitive ? mPrimitive->copy() : nullptr;
    if (mPrimitive) {
      primitiveMap[mPrimitive] = primitive;
    }
    return new RefusingStep(primitive, mParticipatesInBuild);
  }

  void execute(bw::core::LayerBuildContext& context) const override {
    mObservedBuildPrimitives = context.getBuildPrimitives();
    if (mPrimitive) {
      context.appendPrimitive(mPrimitive);
    }
  }

  bool primitivesParticipateInBuild() const override {
    return mParticipatesInBuild;
  }

  bool permitsDirectPrimitiveEditing() const override {
    return false;
  }

  bool acceptsNewPrimitives() const override {
    return false;
  }

  uint32_t adoptPrimitive(bw::core::Primitive* primitive) override {
    if (mPrimitive) {
      throw bw::core::CoreException("RefusingStep already owns a Primitive");
    }
    mPrimitive = primitive;
    return 0;
  }

  void replacePrimitive(
      bw::core::Primitive* oldPrimitive,
      bw::core::Primitive* newPrimitive) override {
    if (oldPrimitive != mPrimitive) {
      throw bw::core::CoreException("Primitive not owned by RefusingStep");
    }
    delete mPrimitive;
    mPrimitive = newPrimitive;
  }

  bool ownsPrimitive(bw::core::Primitive const* primitive) const override {
    return mPrimitive == primitive;
  }

  std::vector<bw::core::Primitive*> const& observedBuildPrimitives() const {
    return mObservedBuildPrimitives;
  }

private:
  mutable std::vector<bw::core::Primitive*> mObservedBuildPrimitives;

  void serializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return true;
  }
};

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

void movingAStepReordersItAndRejectsMovesInvolvingIndexZero() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({10.0f}));
  layer.addStep(makeField({20.0f}));

  auto* movedStep = layer.getStep(1);

  layer.moveStep(1, 2);

  require(layer.getStep(2) == movedStep, "moveStep did not move the step to its target index");
  require(builtPositions(layer) == std::vector<float>({0.0f, 20.0f, 10.0f}),
          "moving a step did not change the order Primitives were built in");

  layer.moveStep(2, 1);

  require(layer.getStep(1) == movedStep, "moveStep did not move the step back");
  require(builtPositions(layer) == std::vector<float>({0.0f, 10.0f, 20.0f}),
          "moving a step back did not restore the original build order");

  requireCoreException(
      [&] { layer.moveStep(0, 1); },
      "a Layer allowed its first step to be moved out of index 0");
  requireCoreException(
      [&] { layer.moveStep(1, 0); },
      "a Layer allowed another step to be moved into index 0");
  require(layer.getStep(0)->getType() == "PrimitiveField" && layer.getNumSteps() == 3,
          "a rejected moveStep call disturbed the Layer's step list");
}

void aNewLayersActiveStepIsTheFirstStepAndAddPrimitiveWritesIntoIt() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  require(layer.getActiveStepIndex() == 0, "a new Layer's active step was not the first step");
  require(layer.getActiveStep() == layer.getStep(0),
          "a new Layer's active step was not identical to its first step");

  layer.addPrimitive(makeRectangle(0.0f));

  require(layer.getPrimitiveField()->getNumPrimitives() == 1,
          "addPrimitive did not write into the active (first) step by default");
}

void selectingAnotherStepRedirectsWhereAddPrimitiveWrites() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  auto secondIndex = layer.addStep(makeField({}));
  auto* secondField = static_cast<bw::core::PrimitiveField*>(layer.getStep(secondIndex));

  layer.setActiveStep(secondIndex);

  require(layer.getActiveStepIndex() == secondIndex, "setActiveStep did not move the active step");
  require(layer.getActiveStep() == secondField, "setActiveStep did not track the given step's identity");

  auto* authored = makeRectangle(10.0f);
  layer.addPrimitive(authored);

  require(secondField->getNumPrimitives() == 1 && secondField->getPrimitive(0) == authored,
          "selecting the second step did not redirect addPrimitive into it");
  require(layer.getPrimitiveField()->getNumPrimitives() == 1,
          "selecting another step let addPrimitive still reach the first step");

  requireCoreException(
      [&] { layer.setActiveStep(layer.getNumSteps()); },
      "setActiveStep accepted an out-of-bounds index");
}

void addPrimitiveIsRejectedWhenTheActiveStepIsDisabled() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto secondIndex = layer.addStep(makeField({}));
  layer.setActiveStep(secondIndex);
  layer.setStepEnabled(secondIndex, false);

  requireCoreException(
      [&] { layer.addPrimitive(makeRectangle(0.0f)); },
      "addPrimitive succeeded while its active step was disabled");
}

void insertRemoveAndMoveStepTrackTheActiveStepsIdentity() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto middleIndex = layer.addStep(makeField({}));
  auto* middleStep = layer.getStep(middleIndex);
  layer.addStep(makeField({}));

  layer.setActiveStep(middleIndex);

  // Inserting before the active step's index shifts it along.
  layer.insertStep(1, makeField({}));
  require(layer.getActiveStep() == middleStep,
          "inserting a step before the active step lost track of its identity");

  auto activeAfterInsert = layer.getActiveStepIndex();

  // Removing a step before the active step shifts it back down.
  layer.removeStep(1);
  require(layer.getActiveStep() == middleStep && layer.getActiveStepIndex() == activeAfterInsert - 1,
          "removing a step before the active step did not track its new position");

  // Removing the active step itself falls back to the first step.
  layer.removeStep(layer.getActiveStepIndex());
  require(layer.getActiveStepIndex() == 0,
          "removing the active step did not fall back to the first step");

  layer.addStep(makeField({}));
  layer.setActiveStep(1);
  auto* activeStep = layer.getStep(1);
  layer.moveStep(1, 2);
  require(layer.getActiveStep() == activeStep && layer.getActiveStepIndex() == 2,
          "moving the active step did not track its new position");
}

void buildStepCapabilitiesAreDeliberateAndPrimitiveFieldPermitsBoth() {
  bw::core::PrimitiveField field;
  RefusingStep refusing;

  require(field.permitsDirectPrimitiveEditing(),
          "PrimitiveField did not permit direct Primitive editing");
  require(field.acceptsNewPrimitives(),
          "PrimitiveField did not accept new Primitives");
  require(!refusing.permitsDirectPrimitiveEditing(),
          "the refusing test step permitted direct Primitive editing");
  require(!refusing.acceptsNewPrimitives(),
          "the refusing test step accepted new Primitives");
}

void aRefusingActiveStepDoesNotAcceptNewPrimitives() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  auto refusingIndex = layer.addStep(new RefusingStep(makeRectangle(10.0f)));
  auto* produced = layer.getPrimitive(0);

  require(layer.getOwningStepIndex(produced) == refusingIndex,
          "getOwningStepIndex did not attribute a derived Primitive to a non-field step");

  layer.setActiveStep(refusingIndex);
  requireCoreException(
      [&] { layer.addPrimitive(makeRectangle(20.0f)); },
      "addPrimitive redirected to another step when the active step refused new Primitives");
}

void stepsReceiveOnlyBuildParticipatingPrimitives() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  auto* participating = makeRectangle(0.0f);
  layer.addPrimitive(participating);
  layer.addStep(new RefusingStep(makeRectangle(10.0f), false));
  auto observerIndex = layer.addStep(new RefusingStep());
  auto* observer = static_cast<RefusingStep*>(layer.getStep(observerIndex));

  require(observer->observedBuildPrimitives() ==
              std::vector<bw::core::Primitive*>({participating}),
          "a step received a derived Primitive excluded from the build");
}

void primitiveStorageIsDispatchedToTheOwningStep() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  layer.addStep(new RefusingStep(makeRectangle(10.0f)));

  auto* replacement = makeRectangle(20.0f);
  layer.replacePrimitive(0, replacement);

  require(layer.getNumPrimitives() == 1 && layer.getPrimitive(0) == replacement,
          "replacing a Primitive owned by a non-PrimitiveField step did not use that step's storage");
  require(layer.getOwningStepIndex(replacement) == 1,
          "replacing a Primitive changed its owning step");

  layer.removePrimitive(replacement);
  require(layer.getNumPrimitives() == 0,
          "removing a Primitive owned by a non-PrimitiveField step did not use that step's storage");
}

void getOwningStepIndexFindsWhichStepProducedAPrimitive() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* first = makeRectangle(0.0f);
  layer.addPrimitive(first);
  auto secondIndex = layer.addStep(makeField({10.0f}));
  auto* second = static_cast<bw::core::PrimitiveField*>(layer.getStep(secondIndex))->getPrimitive(0);

  require(layer.getOwningStepIndex(first) == 0,
          "getOwningStepIndex did not attribute a first-step Primitive to step 0");
  require(layer.getOwningStepIndex(second) == secondIndex,
          "getOwningStepIndex did not attribute a second-step Primitive to its step");

  bw::core::RectanglePolygon stray(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  require(layer.getOwningStepIndex(&stray) == ~0u,
          "getOwningStepIndex did not report ~0u for a Primitive owned by no step here");
}

void copyingALayerCopiesItsStepsAndRebuildsFromThem() {
  bw::core::Layer layer(5, "Source", 100.0f, 10.0f);

  layer.addPrimitive(makeRectangle(0.0f));
  layer.addStep(makeField({10.0f}));
  auto disabled = layer.addStep(makeField({20.0f}));
  layer.setStepEnabled(disabled, false);
  layer.setActiveStep(1);

  bw::core::Layer copy(layer);

  require(copy.getNumSteps() == 3, "a copied Layer did not preserve its step list");
  require(copy.getActiveStepIndex() == 0,
          "a copied Layer's active step was not reset to the first step");
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
    movingAStepReordersItAndRejectsMovesInvolvingIndexZero();
    aNewLayersActiveStepIsTheFirstStepAndAddPrimitiveWritesIntoIt();
    selectingAnotherStepRedirectsWhereAddPrimitiveWrites();
    addPrimitiveIsRejectedWhenTheActiveStepIsDisabled();
    insertRemoveAndMoveStepTrackTheActiveStepsIdentity();
    buildStepCapabilitiesAreDeliberateAndPrimitiveFieldPermitsBoth();
    aRefusingActiveStepDoesNotAcceptNewPrimitives();
    stepsReceiveOnlyBuildParticipatingPrimitives();
    primitiveStorageIsDispatchedToTheOwningStep();
    getOwningStepIndexFindsWhichStepProducedAPrimitive();
    copyingALayerCopiesItsStepsAndRebuildsFromThem();
    std::cout << "A Layer derives its Primitives by running its enabled LayerBuildSteps in order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
