#include "core/DefaultWorldDataGenerator.h"

#include <algorithm>

#include "core/Defines.h"
#include "core/World.h"

namespace bw::core {
DefaultWorldDataGenerator::DefaultWorldDataGenerator() = default;
DefaultWorldDataGenerator::~DefaultWorldDataGenerator() = default;

DefaultWorldDataGenerator::DefaultWorldDataGenerator(
    DefaultWorldDataGenerator const& other) {
  WorldDataGenerator::copyFrom(other);
  mWorldData = other.mWorldData;
}

DefaultWorldDataGenerator& DefaultWorldDataGenerator::operator=(
    DefaultWorldDataGenerator const& other) {
  WorldDataGenerator::copyFrom(other);
  mWorldData = other.mWorldData;
  return *this;
}

WorldDataGenerator* DefaultWorldDataGenerator::copy() {
  return new DefaultWorldDataGenerator(*this);
}

WorldDataPtr DefaultWorldDataGenerator::getWorldData(World const* world) {
  generate(world, false);
  return mWorldData;
}

void DefaultWorldDataGenerator::generate(
    World const* world,
    bool regetPrimitives) {
  BW_UNUSED(regetPrimitives);
  auto primitives = getPrimitives(world, getActiveLayer());
  std::stable_sort(
      primitives.begin(), primitives.end(), SortPrimitivesByPriority());
  ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  mWorldData = std::make_shared<ArrangementWorldData>(
      generator.getWorldData(),
      world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      world->getStepThreshold());
}
}  // namespace bw::core
