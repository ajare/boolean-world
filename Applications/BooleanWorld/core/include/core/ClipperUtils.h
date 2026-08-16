#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include "core/Clipper2Polygon.h"
#include "core/Platform.h"
#include "core/Vertex.h"

namespace bw::core {
class Primitive;

// Path conversion shared by the arrangement builder and floored. Boolean
// clipping is not performed here.
class BW_API ClipperUtils {
public:
  static std::vector<Clipper2Polygon> convertPrimitiveToClipperPolygons(
      Primitive const* primitive);

  static Clipper2Lib::Paths64 convertComplexPolygonsToPath(
      std::vector<ComplexPolygon> const& complexPolygons);

  static Clipper2Lib::Paths64 convertComplexPolygonsToPath(
      Primitive const* primitive);
};
}  // namespace bw::core
