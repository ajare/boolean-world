#include "core/ArrangementWorldDataGenerator.h"

#include <algorithm>
#include <stdexcept>

#include "core/Primitive.h"
#include "core/World.h"
#include "core/WorldDataGenerator.h"

namespace bw::core {
std::vector<arr::Contour> ConvertPrimitiveToContours(
    Primitive const& primitive) {
  std::vector<arr::Contour> contours;
  for (auto const& complexPolygon : primitive.getVertices()) {
    for (auto const& polygon : complexPolygon) {
      arr::Contour contour;
      contour.reserve(polygon.size());
      for (auto const& vertex : polygon) {
        contour.push_back(
            {arr::ToFixedPointCoordinate(vertex.p.x),
             arr::ToFixedPointCoordinate(vertex.p.y)});
      }
      contours.push_back(std::move(contour));
    }
  }
  return contours;
}

std::vector<arr::ArrangementPrimitive> SnapshotPrimitives(
    std::vector<Primitive*> const& primitives) {
  std::vector<arr::ArrangementPrimitive> result;
  result.reserve(primitives.size());
  for (auto primitive : primitives) {
    result.push_back({ConvertPrimitiveToContours(*primitive),
                      primitive->getOperation(),
                      primitive->getFillRule(),
                      primitive->getPriority(),
                      primitive->getId(),
                      primitive->getProperties()});
  }
  return result;
}

ArrangementWorldDataGenerator::ArrangementWorldDataGenerator()
    : mWorldData(arr::BuildArrangement({})) {
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
      WorldDataGenerator::SortPrimitivesByPriority());
  generate(primitives);
}

void ArrangementWorldDataGenerator::generate(
    std::vector<Primitive*> const& primitives) {
  mWorldData = arr::BuildArrangement(SnapshotPrimitives(primitives));
}

arr::ArrangementResultPtr ArrangementWorldDataGenerator::getWorldData() const {
  return mWorldData;
}
}  // namespace bw::core
