#include "core/Vertex.h"

namespace bw {
namespace core {
using namespace std;

wp::BoundingBox calculatePolygonBounds(ClosedPolygon const& polygon) {
  float xMin{1e10f}, yMin{1e10f}, xMax{-1e10f}, yMax{-1e10f};

  for (auto const& vertex : polygon) {
    if (vertex.p.x < xMin) {
      xMin = vertex.p.x;
    }
    if (vertex.p.y < yMin) {
      yMin = vertex.p.y;
    }
    if (vertex.p.x > xMax) {
      xMax = vertex.p.x;
    }
    if (vertex.p.y > yMax) {
      yMax = vertex.p.y;
    }
  }

  return wp::BoundingBox(xMin, yMin, xMax - xMin, yMax - yMin);
}

wp::BoundingBox calculatePolygonBounds(ComplexPolygon const& polygon) {
  float xMin{1e10f}, yMin{1e10f}, xMax{-1e10f}, yMax{-1e10f};

  for (auto const& p : polygon) {
    for (auto const& vertex : p) {
      if (vertex.p.x < xMin) {
        xMin = vertex.p.x;
      }
      if (vertex.p.y < yMin) {
        yMin = vertex.p.y;
      }
      if (vertex.p.x > xMax) {
        xMax = vertex.p.x;
      }
      if (vertex.p.y > yMax) {
        yMax = vertex.p.y;
      }
    }
  }

  return wp::BoundingBox(xMin, yMin, xMax - xMin, yMax - yMin);
}

wp::BoundingBox calculatePolygonBounds(vector<ComplexPolygon> const& polygons) {
  float xMin{1e10f}, yMin{1e10f}, xMax{-1e10f}, yMax{-1e10f};

  for (auto const& polygon : polygons) {
    for (auto const& p : polygon) {
      for (auto const& vertex : p) {
        if (vertex.p.x < xMin) {
          xMin = vertex.p.x;
        }
        if (vertex.p.y < yMin) {
          yMin = vertex.p.y;
        }
        if (vertex.p.x > xMax) {
          xMax = vertex.p.x;
        }
        if (vertex.p.y > yMax) {
          yMax = vertex.p.y;
        }
      }
    }
  }

  return wp::BoundingBox(xMin, yMin, xMax - xMin, yMax - yMin);
}

}  // namespace core
}  // namespace bw