#pragma once

#include <vector>
#include <cstdint>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "Platform.h"

namespace bw {
namespace core {

struct Vertex {
  wp::Vector2 p;

  Vertex() = default;
  Vertex(wp::Vector2 const& position)
      : p(position) {
  }
};

typedef std::vector<Vertex> ClosedPolygon;

typedef std::vector<Vertex> OpenPolygon;

typedef std::vector<Vertex> VertexList;

typedef std::vector<ClosedPolygon> ComplexPolygon;

wp::BoundingBox calculatePolygonBounds(ClosedPolygon const& polygon);

wp::BoundingBox calculatePolygonBounds(ComplexPolygon const& polygon);

wp::BoundingBox calculatePolygonBounds(std::vector<ComplexPolygon> const& polygons);

}  // namespace core
}  // namespace bw
