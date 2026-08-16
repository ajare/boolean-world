#include "core/ArrangementWorldDataGenerator.h"

#include <algorithm>
#include <stdexcept>

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

void ArrangementWorldDataGenerator::setLayerSelection(
    LayerSelection const& selection) {
  if (selection.none()) {
    throw std::invalid_argument("layer selection must not be empty");
  }
  mLayerSelection = selection;
}

LayerSelection const& ArrangementWorldDataGenerator::getLayerSelection() const {
  return mLayerSelection;
}

void ArrangementWorldDataGenerator::setActiveLayer(uint8_t layer) {
  setLayerSelection(SelectLayer(layer));
}

void ArrangementWorldDataGenerator::generate(World const* world) {
  std::vector<Primitive*> primitives;
  for (auto primitive : world->getPrimitives()) {
    auto layer = primitive->getLayer();
    if (layer == BW_LAYER_ALL || mLayerSelection.test(size_t(layer))) {
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
