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
  // A Primitive no longer carries a layer tag to filter on: it belongs to
  // whichever Layer owns it, and World's facade exposes the active Layer's
  // content. Folding across the selected set of Layer ids is #162; until
  // then every Primitive the World hands out participates.
  BW_UNUSED(selection);

  auto primitives = world.getPrimitives();

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

void WorldDataGenerator::rebindToWorld(World const* world) {
  BW_UNUSED(world);
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
