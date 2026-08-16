#include "core/ClipperUtils.h"

#include <cmath>

#include "core/ClipperDefines.h"
#include "core/Primitive.h"

namespace bw::core {
std::vector<Clipper2Polygon> ClipperUtils::convertPrimitiveToClipperPolygons(
    Primitive const* primitive) {
  auto paths = convertComplexPolygonsToPath(primitive);
  std::vector<Clipper2Polygon> polygons;
  polygons.reserve(paths.size());
  for (uint32_t i = 0; i < uint32_t(paths.size()); ++i) {
    polygons.push_back(
        {i != 0 ||
             primitive->getOperation() == Primitive::Operation::Difference,
         primitive->getId(),
         std::move(paths[i])});
  }
  return polygons;
}

Clipper2Lib::Paths64 ClipperUtils::convertComplexPolygonsToPath(
    std::vector<ComplexPolygon> const& complexPolygons) {
  Clipper2Lib::Paths64 paths;
  for (auto const& complexPolygon : complexPolygons) {
    for (auto const& polygon : complexPolygon) {
      Clipper2Lib::Path64 path;
      path.reserve(polygon.size());
      for (auto const& vertex : polygon) {
#ifdef USINGZ
        path.emplace_back(
            int64_t(std::llround(vertex.p.x * BW_CLIPPER_SCALE)),
            int64_t(std::llround(vertex.p.y * BW_CLIPPER_SCALE)),
            0);
#else
        path.emplace_back(
            int64_t(std::llround(vertex.p.x * BW_CLIPPER_SCALE)),
            int64_t(std::llround(vertex.p.y * BW_CLIPPER_SCALE)));
#endif
      }
      paths.push_back(std::move(path));
    }
  }
  return paths;
}

Clipper2Lib::Paths64 ClipperUtils::convertComplexPolygonsToPath(
    Primitive const* primitive) {
  return convertComplexPolygonsToPath(primitive->getVertices());
}
}  // namespace bw::core
