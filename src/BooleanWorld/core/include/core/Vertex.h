#pragma once

#include <vector>
#include <cstdint>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "Platform.h"
#include "core/Defines.h"

namespace bw {
namespace core {

struct Vertex {
  wp::Vector2 p;

  // Flags for the edge FROM this vertex TO THE NEXT vertex in its Ring (the
  // "outgoing edge"). See BW_MESH_EDGE_COLLIDES_FLAG and
  // BW_MESH_EDGE_INVISIBLE_FLAG in Defines.h - the latter is inverted
  // polarity (set = hidden) specifically so this all-bits-clear-but-collides
  // default reads as both colliding and visible.
  uint32_t edgeFlags{BW_MESH_EDGE_COLLIDES_FLAG};

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
