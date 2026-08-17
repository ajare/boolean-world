#include "core/WorldDataGenerator.h"

#include <algorithm>
#include <stdexcept>

#include "core/Defines.h"
#include "core/Utils.h"
#include "core/World.h"

namespace bw::core {
using namespace std;

vector<Primitive*> selectAndOrderPrimitives(
    World const& world, LayerSelection const& selection) {
  vector<Primitive*> primitives;
  for (auto primitive : world.getPrimitives()) {
    auto const layer = primitive->getLayer();
    if (layer == BW_LAYER_ALL || selection.test(size_t(layer))) {
      primitives.push_back(primitive);
    }
  }
  stable_sort(
      primitives.begin(), primitives.end(),
      WorldDataGenerator::SortPrimitivesByPriority());
  return primitives;
}

WorldDataGenerator::WorldDataGenerator()
    : mViewTriangle{} {
}

WorldDataGenerator::~WorldDataGenerator() = default;

WorldDataGenerator::WorldDataGenerator(WorldDataGenerator const& other) {
  copyFrom(other);
}

WorldDataGenerator& WorldDataGenerator::operator=(
    WorldDataGenerator const& other) {
  copyFrom(other);
  return *this;
}

void WorldDataGenerator::copyFrom(WorldDataGenerator const& other) {
  mLayerSelection = other.mLayerSelection;
  mViewTriangle = other.mViewTriangle;
}

WorldDataGenerator* WorldDataGenerator::copyForWorld(World const* world) {
  BW_UNUSED(world);
  return copy();
}

void WorldDataGenerator::setLayerSelection(
    LayerSelection const& selection) {
  if (selection.none()) {
    throw std::invalid_argument("layer selection must not be empty");
  }
  if (selection != mLayerSelection) {
    mLayerSelection = selection;
    handleLayerSelectionChanged();
  }
}

LayerSelection const& WorldDataGenerator::getLayerSelection() const {
  return mLayerSelection;
}

void WorldDataGenerator::setActiveLayer(uint8_t layer) {
  setLayerSelection(SelectLayer(layer));
}

void WorldDataGenerator::handleEvents(uint32_t events) {
  BW_UNUSED(events);
}

void WorldDataGenerator::handleLayerSelectionChanged() {
}

void WorldDataGenerator::update(
    float frameTime,
    WorldUpdateData const& data,
    uint32_t events) {
  BW_UNUSED(frameTime);
  auto const v0 = data.entityPosition;
  auto const [v1, v2] = calculateFovTriangle(v0, data.entityAngle, data.entityViewDist, data.entityFov);
  mViewTriangle = {v0, v1, v2};
  handleEvents(events);
}
}  // namespace bw::core
