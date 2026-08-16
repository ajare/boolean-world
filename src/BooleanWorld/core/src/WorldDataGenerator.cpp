#include "core/WorldDataGenerator.h"

#include <cmath>
#include <stdexcept>

#include "core/Defines.h"
#include "core/World.h"

namespace bw::core {
using namespace std;

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

vector<Primitive*> WorldDataGenerator::getPrimitives(
    World const* world) const {
  vector<Primitive*> primitives;
  for (auto primitive : world->getPrimitives()) {
    auto primitiveLayer = primitive->getLayer();
    if (primitiveLayer == BW_LAYER_ALL ||
        mLayerSelection.test(size_t(primitiveLayer))) {
      primitives.push_back(primitive);
    }
  }
  return primitives;
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
  auto halfFov = data.entityFov * 0.5f;
  auto viewDistance =
      data.entityViewDist * 1.1f / cosf(WP_DEGTORAD(halfFov));
  mViewTriangle = {
      data.entityPosition,
      data.entityPosition +
          wp::Vector2::fromAngle(
              data.entityAngle - halfFov, wp::Clockwise) *
              viewDistance,
      data.entityPosition +
          wp::Vector2::fromAngle(
              data.entityAngle + halfFov, wp::Clockwise) *
              viewDistance};
  handleEvents(events);
}
}  // namespace bw::core
