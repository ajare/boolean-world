#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/YamlSerializer.h>

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

void theStepRegistryEnumeratesAndInstantiatesEachRegisteredType() {
  auto const types = bw::core::LayerBuildStep::getRegisteredTypes();
  require(std::find(types.begin(), types.end(), "PrimitiveField") != types.end(),
          "the step Registry did not enumerate PrimitiveField");

  for (auto const& type : types) {
    auto step = std::unique_ptr<bw::core::LayerBuildStep>(
        bw::core::LayerBuildStep::instantiate(type));
    require(step->getType() == type,
            "the step Registry did not instantiate its enumerated type");
  }
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

  void releasePrimitive(bw::core::Primitive* primitive) override {
    if (primitive != mPrimitive) {
      throw bw::core::CoreException("Primitive not owned by RefusingStep");
    }
    mPrimitive = nullptr;
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

// A step whose execute() always throws, for docs/adr/0039 coverage: a step
// that fails is caught at the execute() boundary rather than propagating.
class ThrowingStep final : public bw::core::LayerBuildStep {
public:
  std::string getType() const override {
    return "ThrowingStep";
  }

  bool mayBeFirstStep() const override {
    return false;
  }

  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*, bw::core::VertexTransformerObject*>&) const override {
    return new ThrowingStep();
  }

  void execute(bw::core::LayerBuildContext&) const override {
    throw bw::core::CoreException("ThrowingStep deliberately failed");
  }

  bool primitivesParticipateInBuild() const override {
    return true;
  }

  bool permitsDirectPrimitiveEditing() const override {
    return false;
  }

  bool acceptsNewPrimitives() const override {
    return false;
  }

  uint32_t adoptPrimitive(bw::core::Primitive*) override {
    throw bw::core::CoreException("ThrowingStep cannot own Primitives");
  }

  void replacePrimitive(bw::core::Primitive*, bw::core::Primitive*) override {
  }

  void releasePrimitive(bw::core::Primitive*) override {
  }

  bool ownsPrimitive(bw::core::Primitive const*) const override {
    return false;
  }

private:
  void serializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeArgs(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return true;
  }
};

// Captures what LayerBuildContext reports for extents and area queries, so
// this is verifiable purely in C++ with no scripting involved.
class AreaQueryStep final : public bw::core::LayerBuildStep {
  std::vector<float> mPositionsToAppend;

  // Cleared and refilled on each execute(), following PrefabField's
  // precedent for a step that derives the Primitives it owns.
  mutable std::vector<std::unique_ptr<bw::core::Primitive>> mBuiltPrimitives;

  mutable wp::BoundingBox mObservedExtents;

  // Parallel to mPositionsToAppend: what the area query saw over that
  // position's probe bounds just before, and just after, that position was
  // appended.
  mutable std::vector<std::vector<bw::core::Primitive*>> mQueriesBeforeAppend;
  mutable std::vector<std::vector<bw::core::Primitive*>> mQueriesAfterAppend;

  // One query made far from anything placed, to check the empty case.
  mutable std::vector<bw::core::Primitive*> mQueryMatchingNothing;

public:
  explicit AreaQueryStep(std::vector<float> positionsToAppend = {})
      : mPositionsToAppend(std::move(positionsToAppend)) {
  }

  // A probe guaranteed to overlap a rectangle placed at x by makeRectangle,
  // whatever margin this build's bounds calculation happens to use.
  static wp::BoundingBox probeAround(float x) {
    std::unique_ptr<bw::core::Primitive> probeSource(makeRectangle(x));
    return probeSource->getBounds();
  }

  std::string getType() const override {
    return "AreaQueryStep";
  }

  bool mayBeFirstStep() const override {
    return false;
  }

  bw::core::LayerBuildStep* copy(
      std::map<bw::core::VertexTransformerObject const*, bw::core::VertexTransformerObject*>&) const override {
    return new AreaQueryStep(mPositionsToAppend);
  }

  void execute(bw::core::LayerBuildContext& context) const override {
    mBuiltPrimitives.clear();
    mObservedExtents = context.getExtents();
    mQueriesBeforeAppend.clear();
    mQueriesAfterAppend.clear();

    for (auto x : mPositionsToAppend) {
      auto probe = probeAround(x);
      // Recorded before appending: self-avoidance depends on this not yet
      // including the Primitive about to be placed at x.
      mQueriesBeforeAppend.push_back(context.findBuildPrimitivesOverlapping(probe));

      auto primitive = std::unique_ptr<bw::core::Primitive>(makeRectangle(x));
      auto* raw = primitive.get();
      mBuiltPrimitives.push_back(std::move(primitive));
      context.appendPrimitive(raw);

      // Recorded right after: this is what makes the query see the
      // executing step's own appends so far, not only prior steps' output.
      mQueriesAfterAppend.push_back(context.findBuildPrimitivesOverlapping(probe));
    }

    mQueryMatchingNothing = context.findBuildPrimitivesOverlapping(probeAround(-1000000.0f));
  }

  bool primitivesParticipateInBuild() const override {
    return true;
  }

  bool permitsDirectPrimitiveEditing() const override {
    return false;
  }

  bool acceptsNewPrimitives() const override {
    return false;
  }

  uint32_t adoptPrimitive(bw::core::Primitive*) override {
    throw bw::core::CoreException("AreaQueryStep does not accept Primitives");
  }

  void replacePrimitive(bw::core::Primitive*, bw::core::Primitive*) override {
    throw bw::core::CoreException("AreaQueryStep output cannot be edited directly");
  }

  void releasePrimitive(bw::core::Primitive*) override {
    throw bw::core::CoreException("AreaQueryStep output cannot be moved to another step");
  }

  bool ownsPrimitive(bw::core::Primitive const* primitive) const override {
    return std::any_of(
        mBuiltPrimitives.begin(), mBuiltPrimitives.end(),
        [primitive](auto const& owned) { return owned.get() == primitive; });
  }

  wp::BoundingBox const& observedExtents() const {
    return mObservedExtents;
  }

  std::vector<std::vector<bw::core::Primitive*>> const& queriesBeforeAppend() const {
    return mQueriesBeforeAppend;
  }

  std::vector<std::vector<bw::core::Primitive*>> const& queriesAfterAppend() const {
    return mQueriesAfterAppend;
  }

  std::vector<bw::core::Primitive*> const& queryMatchingNothing() const {
    return mQueryMatchingNothing;
  }

private:
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

void layerBuildStepIdsRemainStableAcrossRecipeChangesAndSerialization() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  auto const firstId = layer.getStep(0)->getId();
  auto const secondIndex = layer.addStep(makeField({10.0f}));
  auto* secondStep = layer.getStep(secondIndex);
  auto const secondId = secondStep->getId();
  auto const thirdIndex = layer.addStep(makeField({20.0f}));
  auto* thirdStep = layer.getStep(thirdIndex);
  auto const thirdId = thirdStep->getId();

  require(firstId != secondId && secondId != thirdId && firstId != thirdId,
          "a Layer assigned duplicate build step ids");

  layer.moveStep(secondIndex, thirdIndex);
  require(secondStep->getId() == secondId && thirdStep->getId() == thirdId,
          "moving build steps changed their ids");

  layer.removeStep(1);
  auto const replacementIndex = layer.addStep(makeField({30.0f}));
  auto const replacementId = layer.getStep(replacementIndex)->getId();
  require(replacementId != thirdId && replacementId > thirdId,
          "removing a build step allowed its id to be reused");

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeData;
  layer.serialize(writer, writeData);
  writer->serialize();

  bw::core::Layer loaded;
  auto reader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();
  bw::core::SerializationWorkData readData;
  readData.accelGridSize = 10.0f;
  require(loaded.deserialize(reader, readData),
          "a Layer with build step ids failed to deserialize");
  require(loaded.getStep(0)->getId() == firstId &&
              loaded.getStep(1)->getId() == secondId &&
              loaded.getStep(2)->getId() == replacementId,
          "LayerBuildStep ids did not round-trip through serialization");

  auto const afterLoadIndex = loaded.addStep(makeField({40.0f}));
  require(loaded.getStep(afterLoadIndex)->getId() > replacementId,
          "deserializing a Layer allowed a removed build step id to be reused");
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

void aPrimitiveMovesBetweenStepsOfTheSameTypeAndFoldsInItsNewStepsPlace() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* moved = makeRectangle(0.0f);
  layer.addPrimitive(moved);
  auto const secondIndex = layer.addStep(makeField({10.0f}));

  require(layer.getOwningStepIndex(moved) == 0,
          "the fixture Primitive did not start in step 0");
  require(layer.getPrimitive(0) == moved,
          "a step 0 Primitive did not fold before a later step's Primitive");

  require(layer.canMovePrimitiveToStep(moved, secondIndex),
          "a move between two PrimitiveField steps was not permitted");
  layer.movePrimitiveToStep(moved, secondIndex);

  require(layer.getOwningStepIndex(moved) == secondIndex,
          "the moved Primitive is not attributed to its new step");
  require(layer.getPrimitiveField()->getNumPrimitives() == 0,
          "the moved Primitive was left behind in its old step");
  require(layer.getNumPrimitives() == 2,
          "moving a Primitive between steps changed how many the Layer derives");
  require(layer.getPrimitive(1) == moved,
          "the moved Primitive did not take its new step's place in the fold order");
  require(layer.getPrimitive(moved->getId()) == moved,
          "the moved Primitive's id was not re-stamped to its new derived index");
}

void movingAPrimitiveBetweenStepsOfDifferentTypesIsRejected() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* authored = makeRectangle(0.0f);
  layer.addPrimitive(authored);
  auto const prefabsIndex = layer.addStep(new bw::core::DefinePrefabs());
  auto* prefabs = static_cast<bw::core::DefinePrefabs*>(layer.getStep(prefabsIndex));
  prefabs->setSelectedPrefab(prefabs->addPrefab("Target"));
  layer.rebuild();

  require(prefabs->acceptsNewPrimitives(),
          "the fixture DefinePrefabs step did not accept new Primitives");
  require(!layer.canMovePrimitiveToStep(authored, prefabsIndex),
          "a move into a step of another type was reported as permitted");
  requireCoreException(
      [&] { layer.movePrimitiveToStep(authored, prefabsIndex); },
      "a move into a step of another type was not rejected");
  require(layer.getOwningStepIndex(authored) == 0 &&
              layer.getPrimitiveField()->ownsPrimitive(authored),
          "a rejected move disturbed the Primitive's original ownership");
}

void movesToTheSameStepOutOfRangeStepsAndDisabledStepsAreRejected() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* authored = makeRectangle(0.0f);
  layer.addPrimitive(authored);
  auto const disabledIndex = layer.addStep(makeField({10.0f}));
  layer.setStepEnabled(disabledIndex, false);

  require(!layer.canMovePrimitiveToStep(authored, 0),
          "a move into the Primitive's own step was reported as permitted");
  require(!layer.canMovePrimitiveToStep(authored, layer.getNumSteps()),
          "a move to an out-of-range step index was reported as permitted");
  require(!layer.canMovePrimitiveToStep(authored, disabledIndex),
          "a move into a disabled step was reported as permitted");

  requireCoreException(
      [&] { layer.movePrimitiveToStep(authored, disabledIndex); },
      "a move into a disabled step was not rejected");

  bw::core::RectanglePolygon stray(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  require(!layer.canMovePrimitiveToStep(&stray, disabledIndex),
          "a Primitive owned by no step here was reported as movable");
}

void movingAPrimitiveOutOfAStepWhoseOutputCannotBeEditedIsRejected() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* owned = makeRectangle(0.0f);
  layer.addStep(new RefusingStep(owned));
  auto const targetIndex = layer.addStep(new RefusingStep());
  layer.rebuild();

  // Same type on both sides, so only permitsDirectPrimitiveEditing stands
  // between this and a move.
  require(layer.getStep(1)->getType() == layer.getStep(targetIndex)->getType(),
          "the fixture steps were not the same type");
  require(!layer.canMovePrimitiveToStep(owned, targetIndex),
          "a move out of a step that refuses direct editing was permitted");
  requireCoreException(
      [&] { layer.movePrimitiveToStep(owned, targetIndex); },
      "a move out of a step that refuses direct editing was not rejected");
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

// docs/adr/0039: a failed step is caught at the execute() boundary rather
// than propagating out of rebuild(), the Layer keeps what steps before it
// produced, and the failed step plus everything after it contributes
// nothing.
void aFailingStepHaltsTheBuildRetainsEarlierOutputAndRecordsItsFailure() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  layer.getPrimitiveField()->addPrimitive(makeRectangle(0.0f));

  auto const throwingIndex = layer.addStep(new ThrowingStep());
  auto const laterIndex = layer.addStep(makeField({40.0f}));

  require(builtPositions(layer) == std::vector<float>({0.0f}),
          "a failed step's Layer did not retain the Primitives produced before it");
  require(layer.getStep(throwingIndex)->hasFailed(),
          "a step that threw during execute() was not recorded as failed");
  require(!layer.getStep(throwingIndex)->getFailureMessage().empty(),
          "a failed step's failure message was not recorded");
  require(!layer.getStep(laterIndex)->hasFailed(),
          "a step after the one that actually failed was itself marked as failed");
}

// A step that ran and legitimately produced nothing must be distinguishable
// from one that failed - both contribute zero Primitives, but only one
// hasFailed().
void aFailedStepIsDistinguishableFromASuccessfulEmptyStep() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto const emptyFieldIndex = layer.addStep(new bw::core::PrimitiveField());
  require(!layer.getStep(emptyFieldIndex)->hasFailed(),
          "a step that ran successfully and produced nothing was marked as failed");

  auto const throwingIndex = layer.addStep(new ThrowingStep());
  require(layer.getStep(throwingIndex)->hasFailed(),
          "a step that threw during execute() was not marked as failed");
}

// Fixing a failed step (here, simply disabling it) clears its failure on the
// next rebuild and lets the steps after it contribute again.
void disablingAFailedStepClearsItsFailureAndUnblocksLaterSteps() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  layer.getPrimitiveField()->addPrimitive(makeRectangle(0.0f));

  auto const throwingIndex = layer.addStep(new ThrowingStep());
  layer.addStep(makeField({40.0f}));

  require(layer.getStep(throwingIndex)->hasFailed(), "test setup: step did not fail");
  require(builtPositions(layer) == std::vector<float>({0.0f}), "test setup: build did not halt");

  layer.setStepEnabled(throwingIndex, false);

  require(!layer.getStep(throwingIndex)->hasFailed(),
          "disabling a failed step did not clear its recorded failure");
  require(builtPositions(layer) == std::vector<float>({0.0f, 40.0f}),
          "disabling the failed step did not let the steps after it contribute again");
}

// A step's name is authored text distinct from its stable id: unlike the id,
// it is not assigned by the Layer and need not be unique.
void aStepsNameRoundTripsThroughSerializationAndDefaultsToEmpty() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);
  require(layer.getStep(0)->getName().empty(),
          "a newly constructed build step did not default to an empty name");

  auto const secondIndex = layer.addStep(makeField({10.0f}));
  layer.getStep(secondIndex)->setName("spawn points");

  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData writeData;
  layer.serialize(writer, writeData);
  writer->serialize();

  bw::core::Layer loaded;
  auto reader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(writer->getSerializedString()));
  reader->deserialize();
  bw::core::SerializationWorkData readData;
  readData.accelGridSize = 10.0f;
  require(loaded.deserialize(reader, readData),
          "a Layer with named build steps failed to deserialize");

  require(loaded.getStep(0)->getName().empty(),
          "an unnamed build step gained a name across serialization");
  require(loaded.getStep(secondIndex)->getName() == "spawn points",
          "a build step's name did not round-trip through serialization");
}

// docs/adr for #361: LayerBuildContext exposes a Layer's extents so a step
// never needs the Layer itself for them.
void theContextExposesTheLayersExtentsWithoutReachingForTheLayer() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* queryStep = new AreaQueryStep();
  layer.addStep(queryStep);

  require(queryStep->observedExtents().getPosition() == layer.getExtents().getPosition() &&
              queryStep->observedExtents().getSize() == layer.getExtents().getSize(),
          "the context did not report the owning Layer's own extents");
}

// The area query is bounds-overlap only, scoped to getBuildPrimitives(): it
// must see prior steps' output, must see the executing step's own appends so
// far (self-avoidance), and must return empty rather than fail when nothing
// overlaps.
void theAreaQuerySeesPriorOutputAndTheExecutingStepsOwnAppendsSoFar() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto* priorPrimitive = makeRectangle(0.0f);
  layer.addPrimitive(priorPrimitive);

  auto* queryStep = new AreaQueryStep(std::vector<float>({0.0f, 100000.0f}));
  layer.addStep(queryStep);

  layer.rebuild();

  require(queryStep->queriesBeforeAppend().size() == 2 &&
              queryStep->queriesAfterAppend().size() == 2,
          "the fixture step did not run its two scheduled appends");

  require(queryStep->queriesBeforeAppend()[0] == std::vector<bw::core::Primitive*>({priorPrimitive}),
          "the area query did not see a prior step's Primitive overlapping its bounds");

  require(queryStep->queriesBeforeAppend()[1].empty(),
          "the area query saw the step's own Primitive before it was appended");

  auto const& afterSecondAppend = queryStep->queriesAfterAppend()[1];
  require(afterSecondAppend.size() == 1 && afterSecondAppend[0]->getPosition().x == 100000.0f,
          "the area query did not see the executing step's own append made earlier in the same run");

  require(queryStep->queryMatchingNothing().empty(),
          "a query matching nothing did not return empty");
}

void duplicateStepNamesAreAllowedAndFindStepIdByNameResolvesToTheFirstMatch() {
  bw::core::Layer layer(0, "Base", 100.0f, 10.0f);

  auto const secondIndex = layer.addStep(makeField({10.0f}));
  auto* secondStep = layer.getStep(secondIndex);
  secondStep->setName("props");

  auto const thirdIndex = layer.addStep(makeField({20.0f}));
  layer.getStep(thirdIndex)->setName("props");

  require(layer.findStepIdByName("props") == secondStep->getId(),
          "findStepIdByName did not resolve to the first step with a duplicated name");
  require(layer.findStepIdByName("does not exist") == ~0u,
          "findStepIdByName did not report not-found for an unknown name");
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    theStepRegistryEnumeratesAndInstantiatesEachRegisteredType();
    aNewLayerStartsWithOneEmptyPrimitiveFieldStep();
    executingAPrimitiveFieldStepAddsItsEmbeddedPrimitives();
    authoringThroughTheLayerFacadeGoesIntoTheFirstStep();
    multipleEnabledStepsProduceTheirConcatenatedResultInOrder();
    disablingAStepDropsItsPrimitivesUntilItIsEnabledAgain();
    disablingTheFirstStepIsAllowedButDeletingAndRetypingItIsNot();
    stepsAreOnlyInsertableAtIndexOneOrAbove();
    removingAStepDropsThePrimitivesItProduced();
    movingAStepReordersItAndRejectsMovesInvolvingIndexZero();
    layerBuildStepIdsRemainStableAcrossRecipeChangesAndSerialization();
    aNewLayersActiveStepIsTheFirstStepAndAddPrimitiveWritesIntoIt();
    selectingAnotherStepRedirectsWhereAddPrimitiveWrites();
    addPrimitiveIsRejectedWhenTheActiveStepIsDisabled();
    insertRemoveAndMoveStepTrackTheActiveStepsIdentity();
    buildStepCapabilitiesAreDeliberateAndPrimitiveFieldPermitsBoth();
    aRefusingActiveStepDoesNotAcceptNewPrimitives();
    stepsReceiveOnlyBuildParticipatingPrimitives();
    primitiveStorageIsDispatchedToTheOwningStep();
    getOwningStepIndexFindsWhichStepProducedAPrimitive();
    aPrimitiveMovesBetweenStepsOfTheSameTypeAndFoldsInItsNewStepsPlace();
    movingAPrimitiveBetweenStepsOfDifferentTypesIsRejected();
    movesToTheSameStepOutOfRangeStepsAndDisabledStepsAreRejected();
    movingAPrimitiveOutOfAStepWhoseOutputCannotBeEditedIsRejected();
    copyingALayerCopiesItsStepsAndRebuildsFromThem();
    aFailingStepHaltsTheBuildRetainsEarlierOutputAndRecordsItsFailure();
    aFailedStepIsDistinguishableFromASuccessfulEmptyStep();
    disablingAFailedStepClearsItsFailureAndUnblocksLaterSteps();
    aStepsNameRoundTripsThroughSerializationAndDefaultsToEmpty();
    duplicateStepNamesAreAllowedAndFindStepIdByNameResolvesToTheFirstMatch();
    theContextExposesTheLayersExtentsWithoutReachingForTheLayer();
    theAreaQuerySeesPriorOutputAndTheExecutingStepsOwnAppendsSoFar();
    std::cout << "A Layer derives its Primitives by running its enabled LayerBuildSteps in order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
