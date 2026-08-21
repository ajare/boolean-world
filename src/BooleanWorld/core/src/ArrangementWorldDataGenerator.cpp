#include "core/ArrangementWorldDataGenerator.h"

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

void ArrangementWorldDataGenerator::generate(
    World const* world, LayerSelection const& selection) {
  generate(selectAndOrderPrimitives(*world, selection));
}

void ArrangementWorldDataGenerator::generate(
    std::vector<Primitive*> const& primitives) {
  mWorldData = arr::BuildArrangement(SnapshotPrimitives(primitives));
}

arr::ArrangementResultPtr ArrangementWorldDataGenerator::getWorldData() const {
  return mWorldData;
}
}  // namespace bw::core
