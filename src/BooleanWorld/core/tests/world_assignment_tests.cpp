#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/DynamicWorldDataGenerator.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TrackingRectangle : public bw::core::RectanglePolygon {
  int* mDestructionCount;

public:
  explicit TrackingRectangle(int& destructionCount)
      : RectanglePolygon(Operation::Union, FillRule::NonZero, 1.0f),
        mDestructionCount(&destructionCount) {
  }

  TrackingRectangle(TrackingRectangle const& other)
      : RectanglePolygon(other),
        mDestructionCount(other.mDestructionCount) {
  }

  ~TrackingRectangle() override {
    ++*mDestructionCount;
  }

  bw::core::Primitive* copy() const override {
    return new TrackingRectangle(*this);
  }
};

class TrackingTriggerLine : public bw::core::WorldTriggerLine {
  int* mDestructionCount;

public:
  explicit TrackingTriggerLine(int& destructionCount)
      : WorldTriggerLine({-4.0f, 0.0f}, {4.0f, 0.0f}),
        mDestructionCount(&destructionCount) {
  }

  ~TrackingTriggerLine() override {
    ++*mDestructionCount;
  }
};

class TrackingGenerator : public bw::core::WorldDataGenerator {
  int* mDestructionCount;

public:
  explicit TrackingGenerator(int& destructionCount)
      : mDestructionCount(&destructionCount) {
  }

  ~TrackingGenerator() override {
    ++*mDestructionCount;
  }

  bw::core::WorldDataGenerator* copy() override {
    return new TrackingGenerator(*mDestructionCount);
  }

  bw::core::WorldDataPtr getWorldData(bw::core::World const*) override {
    return nullptr;
  }

  void generate(bw::core::World const*, bool) override {
  }
};

void assignmentFromEmptyWorldReplacesPopulatedState() {
  int primitiveDestructions = 0;
  int triggerLineDestructions = 0;
  int generatorDestructions = 0;

  {
    bw::core::World source(200.0f, 20.0f);
    source.setName("empty source");

    bw::core::World destination(100.0f, 10.0f);
    destination.addPrimitive(new TrackingRectangle(primitiveDestructions));
    destination.addTriggerLine(new TrackingTriggerLine(triggerLineDestructions));
    destination.setWorldDataGenerator(new TrackingGenerator(generatorDestructions));

    destination = source;

    require(destination.getName() == "empty source",
            "assignment from an empty World did not copy scalar state");
    require(destination.getNumPrimitives() == 0,
            "assignment from an empty World retained primitives");
    require(destination.getNumTriggerLines() == 0,
            "assignment from an empty World retained trigger lines");
    require(destination.getPrimitiveAccelerationGridSize() == 20.0f,
            "assignment from an empty World retained the replaced grid");
    require(primitiveDestructions == 1 && triggerLineDestructions == 1 &&
                generatorDestructions == 1,
            "assignment did not release replaced owned state exactly once");
  }

  require(primitiveDestructions == 1 && triggerLineDestructions == 1 &&
              generatorDestructions == 1,
          "replaced owned state was released more than once");
}

void populatedAssignmentReplacesCollectionsAndSurvivesSourceDestruction() {
  auto source = std::make_unique<bw::core::World>(300.0f, 30.0f);
  source->setName("populated source");
  source->addTriggerLine(new bw::core::WorldTriggerLine(
      {20.0f, -5.0f}, {20.0f, 5.0f},
      bw::core::WorldTriggerLineSide::Both));

  auto* root = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  auto* child = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  root->setPosition({2.0f, 3.0f});
  child->setPosition({10.0f, 0.0f});
  source->addPrimitive(root);
  source->addPrimitive(child);
  child->setParent(root);
  source->setWorldDataGenerator(
      new bw::core::DynamicWorldDataGenerator(source.get()));

  bw::core::World destination(100.0f, 10.0f);
  destination.addPrimitive(new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Difference,
      bw::core::Primitive::FillRule::EvenOdd, 1.0f));
  destination.addTriggerLine(new bw::core::WorldTriggerLine(
      {-10.0f, 0.0f}, {10.0f, 0.0f}));

  destination = *source;

  require(destination.getNumPrimitives() == 2,
          "assignment appended primitives instead of replacing them");
  require(destination.getNumTriggerLines() == 1,
          "assignment appended trigger lines instead of replacing them");
  require(destination.getPrimitive(0) != root && destination.getPrimitive(1) != child,
          "assignment retained source primitive instances");
  require(destination.getTriggerLine(0) != source->getTriggerLine(0),
          "assignment retained a source trigger line instance");

  auto const oldChildVertex = destination.getPrimitive(1)->getVertices()[0][0][0].p;
  source.reset();
  destination.getPrimitive(0)->setPosition({7.0f, 9.0f});
  destination.getPrimitive(1)->updateVertexPositions();

  require(destination.getName() == "populated source" &&
              destination.getPrimitive(1)->getVertices()[0][0][0].p ==
                  oldChildVertex + wp::Vector2(5.0f, 6.0f),
          "assigned World depended on the destroyed source");

  auto* generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      destination.getWorldDataGenerator());
  require(generator != nullptr,
          "assignment did not preserve the source generator type");
  generator->generateBlocking();
  require(destination.getWorldData()->getContainingFaceIndex({7.0f, 9.0f}) != ~0u,
          "assigned generator remained bound to the destroyed source");
}

void selfAssignmentIsANoOp() {
  int primitiveDestructions = 0;
  int triggerLineDestructions = 0;
  int generatorDestructions = 0;

  {
    bw::core::World world(100.0f, 10.0f);
    world.setName("self");
    world.addPrimitive(new TrackingRectangle(primitiveDestructions));
    world.addTriggerLine(new TrackingTriggerLine(triggerLineDestructions));
    world.setWorldDataGenerator(new TrackingGenerator(generatorDestructions));

    auto* primitive = world.getPrimitive(0);
    auto* triggerLine = world.getTriggerLine(0);
    auto* generator = world.getWorldDataGenerator();
    world = world;

    require(world.getName() == "self" && world.getPrimitive(0) == primitive &&
                world.getTriggerLine(0) == triggerLine &&
                world.getWorldDataGenerator() == generator,
            "self-assignment changed World state");
    require(primitiveDestructions == 0 && triggerLineDestructions == 0 &&
                generatorDestructions == 0,
            "self-assignment released owned state");
  }

  require(primitiveDestructions == 1 && triggerLineDestructions == 1 &&
              generatorDestructions == 1,
          "self-assigned World did not remain valid until destruction");
}

}  // namespace

int main() {
  try {
    assignmentFromEmptyWorldReplacesPopulatedState();
    populatedAssignmentReplacesCollectionsAndSurvivesSourceDestruction();
    selfAssignmentIsANoOp();
    std::cout << "World assignment safely replaces owned state\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
