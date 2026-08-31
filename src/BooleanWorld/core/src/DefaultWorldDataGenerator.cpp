#include "core/DefaultWorldDataGenerator.h"

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
  auto entries = selectAndOrderPrimitiveEntries(
      *world, getLayerSelection(), getPrimitiveFilter());
  std::vector<Primitive*> primitives;
  std::vector<uint64_t> priorities;
  primitives.reserve(entries.size());
  priorities.reserve(entries.size());
  for (auto const& entry : entries) {
    primitives.push_back(entry.primitive);
    priorities.push_back(entry.priority);
  }
  ArrangementWorldDataGenerator generator;
  generator.setChipParametersResolver(getChipParametersResolver());
  generator.generateOrdered(primitives, priorities);
  mWorldData = std::make_shared<ArrangementWorldData>(
      generator.getWorldData(),
      world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      world->getWedgeGenerationParameters());
}
}  // namespace bw::core
