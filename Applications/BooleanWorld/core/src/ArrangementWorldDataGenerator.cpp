#include "core/ArrangementWorldDataGenerator.h"

#include <algorithm>

#include "core/ClipperUtils.h"
#include "core/Primitive.h"
#include "core/World.h"

namespace bw::core {
namespace {
std::vector<expr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives) {
  std::vector<expr::ArrangementPrimitive> result;
  result.reserve(primitives.size());
  for (auto primitive : primitives) {
    result.push_back({ClipperUtils::convertComplexPolygonsToPath(primitive),
                      primitive->getOperation(),
                      primitive->getFillRule(),
                      primitive->getPriority(),
                      primitive->getId(),
                      primitive->getProperties()});
  }
  return result;
}
}  // namespace

ArrangementWorldDataGenerator::ArrangementWorldDataGenerator()
    : mWorldData(expr::BuildArrangement({})) {
}

void ArrangementWorldDataGenerator::setActiveLayer(uint8_t layer) {
  mActiveLayer = layer;
}

uint8_t ArrangementWorldDataGenerator::getActiveLayer() const {
  return mActiveLayer;
}

void ArrangementWorldDataGenerator::generate(World const* world) {
  std::vector<Primitive*> primitives;
  for (auto primitive : world->getPrimitives()) {
    auto layer = primitive->getLayer();
    if (layer == mActiveLayer || layer == BW_LAYER_ALL ||
        mActiveLayer == BW_LAYER_ALL) {
      primitives.push_back(primitive);
    }
  }
  std::stable_sort(
      primitives.begin(), primitives.end(),
      [](Primitive const* lhs, Primitive const* rhs) {
        return lhs->getPriority() < rhs->getPriority();
      });
  generate(primitives);
}

void ArrangementWorldDataGenerator::generate(
    std::vector<Primitive*> const& primitives) {
  mWorldData = expr::BuildArrangement(SnapshotPrimitives(primitives));
}

expr::ArrangementResultPtr ArrangementWorldDataGenerator::getWorldData() const {
  return mWorldData;
}
}  // namespace bw::core
