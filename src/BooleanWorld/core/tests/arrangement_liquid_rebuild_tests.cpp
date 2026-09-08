#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>

#include <core/ArrangementWorldData.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

namespace {
using bw::core::ArrangementWorldDataPtr;
using bw::core::ComplexPolygon;
using bw::core::DynamicWorldDataGenerator;
using bw::core::Elevation;
using bw::core::MeshPrimitive;
using bw::core::Primitive;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(
    double actual, double expected, double tolerance,
    std::string const& message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(
        message + ": expected " + std::to_string(expected) + ", got " +
        std::to_string(actual));
  }
}

ComplexPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

MeshPrimitive* region(
    Primitive::Operation operation, float left, float right,
    float liquidLevel = 0.0f, Elevation floor = Elevation{0.0f}) {
  auto* result = MeshPrimitive::fromComplexPolygons(
      operation, {rectangle(left, 0.0f, right, 10.0f)});
  auto properties = result->getProperties();
  properties.floorZ = floor;
  properties.ceilingZ = 40.0f;
  properties.liquidLevel = liquidLevel;
  result->setProperties(properties);
  return result;
}

class RebuildingWorld {
  bw::core::World mWorld{100.0f, 10.0f};
  DynamicWorldDataGenerator mGenerator{&mWorld};

public:
  RebuildingWorld() { mGenerator.setAllowCommitIfVisible(true); }

  bw::core::World& world() { return mWorld; }

  ArrangementWorldDataPtr rebuild() {
    mGenerator.generateBlocking();
    return mGenerator.getWorldData(&mWorld);
  }
};

void aChangingBasinSpillsIntoANewUnion() {
  RebuildingWorld fixture;
  fixture.world().addPrimitive(region(Primitive::Operation::Union, 0, 10, 8));
  auto sealed = fixture.rebuild();
  requireNear(sealed->getLiquidDepth({5.0f, 5.0f}), 8.0, 0.001,
              "the original sealed basin had the wrong Liquid depth");

  fixture.world().addPrimitive(region(Primitive::Operation::Union, 10, 20));
  auto expanded = fixture.rebuild();
  require(expanded != sealed,
          "adding a Union did not publish a new immutable World snapshot");
  requireNear(expanded->getLiquidDepth({5.0f, 5.0f}), 4.0, 0.001,
              "the source Pool did not spill into the new basin");
  requireNear(expanded->getLiquidDepth({15.0f, 5.0f}), 4.0, 0.001,
              "the new Union did not receive spilled Liquid");
  requireNear(sealed->getLiquidDepth({5.0f, 5.0f}), 8.0, 0.001,
              "rebuilding mutated Liquid state in the old snapshot");
}

void aDynamicCutConcentratesConservedAuthoredVolume() {
  RebuildingWorld fixture;
  fixture.world().addPrimitive(region(Primitive::Operation::Union, 0, 10, 2));
  auto whole = fixture.rebuild();

  auto* cut = region(Primitive::Operation::Difference, 5, 10);
  cut->setPriority(1);
  fixture.world().addPrimitive(cut);
  auto carved = fixture.rebuild();

  requireNear(whole->getLiquidDepth({2.5f, 5.0f}), 2.0, 0.001,
              "the original basin had the wrong authored volume");
  requireNear(carved->getLiquidDepth({2.5f, 5.0f}), 4.0, 0.001,
              "a dynamic cut did not concentrate volume into the surviving footprint");
  requireNear(carved->getLiquidDepth({7.5f, 5.0f}), 0.0, 0.001,
              "the carved footprint retained stale local Liquid depth");
  require(carved->getContainingFaceIndex({2.5f, 5.0f}) !=
                  whole->getContainingFaceIndex({2.5f, 5.0f}) ||
              carved->getHydraulicCells().data() !=
                  whole->getHydraulicCells().data(),
          "the cut reused Hydraulic cells from the previous Arrangement");
}

void replacementCanOpenANewExteriorDrain() {
  RebuildingWorld fixture;
  fixture.world().addPrimitive(region(Primitive::Operation::Union, 0, 10, 3));
  auto sealed = fixture.rebuild();
  require(sealed->getLiquidDepth({5.0f, 5.0f}) > 0.0f,
          "the replacement fixture did not begin with a sealed Pool");

  auto* open = region(Primitive::Operation::Union, 0, 10, 3);
  auto proxy = open->createEditingProxy();
  auto edge = proxy->getFirstEdgeIndex();
  require(proxy->setEdgeCollisionOverride(edge, false),
          "the replacement fixture could not open its Border");
  proxy->commitTo(*open);
  fixture.world().replacePrimitive(0, open);

  auto drained = fixture.rebuild();
  requireNear(drained->getLiquidDepth({5.0f, 5.0f}), 0.0, 0.001,
              "replacement with an open Border retained the old sealed Pool");
  require(std::ranges::none_of(
              drained->getLiquidPoolElevations(),
              [](double elevation) { return std::isfinite(elevation); }),
          "a replacement drain retained stale Pool identity");
}

void transformsAndSlopeEditsReplaceEveryCellSample() {
  RebuildingWorld fixture;
  auto* basin = region(Primitive::Operation::Union, 0, 10, 2);
  fixture.world().addPrimitive(basin);
  auto level = fixture.rebuild();

  auto properties = basin->getProperties();
  properties.floorZ = Elevation{0.0f, {0.5f, 0.0f}};
  basin->setProperties(properties);
  auto sloped = fixture.rebuild();
  require(sloped->getLiquidDepth({1.0f, 5.0f}) >
              sloped->getLiquidDepth({8.0f, 5.0f}),
          "a slope edit retained the old face-wide Liquid depth");
  requireNear(level->getHydraulicCells().front().floor.gradient.x, 0.0, 0.001,
              "a slope edit mutated an old Hydraulic cell");
  requireNear(sloped->getHydraulicCells().front().floor.gradient.x, 0.5, 0.001,
              "a rebuilt Hydraulic cell retained the old floor plane");

  basin->setPosition({20.0f, 0.0f});
  basin->updateVertexPositions();
  auto moved = fixture.rebuild();
  requireNear(moved->getLiquidDepth({1.0f, 5.0f}), 0.0, 0.001,
              "a transformed basin retained Liquid at its old face location");
  require(moved->getLiquidDepth({21.0f, 5.0f}) > 0.0f,
          "a transformed basin did not settle Liquid in its new cells");
  require(sloped->getLiquidDepth({1.0f, 5.0f}) > 0.0f,
          "transforming the basin mutated the preceding snapshot");
}

}  // namespace

int main() {
  try {
    aChangingBasinSpillsIntoANewUnion();
    aDynamicCutConcentratesConservedAuthoredVolume();
    replacementCanOpenANewExteriorDrain();
    transformsAndSlopeEditsReplaceEveryCellSample();
    std::cout << "World rebuilds resettle conserved Liquid in fresh Hydraulic cells\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
