#include "core/WorldDataGenerator.h"

#include <cmath>

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
  mActiveLayer = other.mActiveLayer;
  mViewTriangle = other.mViewTriangle;
}

vector<Primitive*> WorldDataGenerator::getPrimitives(
    World const* world,
    uint8_t layer) const {
  vector<Primitive*> primitives;
  for (auto primitive : world->getPrimitives()) {
    auto primitiveLayer = primitive->getLayer();
    if (primitiveLayer == layer || layer == BW_LAYER_ALL ||
        primitiveLayer == BW_LAYER_ALL) {
      primitives.push_back(primitive);
    }
  }
  return primitives;
}

void WorldDataGenerator::setActiveLayer(uint8_t layer) {
  mActiveLayer = layer;
}

uint8_t WorldDataGenerator::getActiveLayer() const {
  return mActiveLayer;
}

void WorldDataGenerator::handleEvents(uint32_t events) {
  BW_UNUSED(events);
}

void WorldDataGenerator::update(
    float frameTime,
    WorldUpdateData const& data,
    uint32_t events) {
  BW_UNUSED(frameTime);
  mActiveLayer = data.activeLayer;

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
