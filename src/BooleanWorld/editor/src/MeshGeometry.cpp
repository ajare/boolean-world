#include "MeshGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <willpower/geometry/Edge.h>
#include <willpower/geometry/Polygon.h>
#include <willpower/geometry/Vertex.h>

namespace editor::meshGeometry {
using namespace std;

bool pointInsideRing(wp::geometry::Mesh const& mesh,
                     wp::geometry::Polygon const& ring,
                     wp::Vector2 const& point) {
  auto vertices = ring.getOrderedVertexIndices();
  if (vertices.size() < 3) return false;
  bool inside = false;
  for (size_t i = 0, previous = vertices.size() - 1;
       i < vertices.size(); previous = i++) {
    auto const& a = mesh.getVertex(vertices[i]).getPosition();
    auto const& b = mesh.getVertex(vertices[previous]).getPosition();
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
}

float twiceSignedArea(vector<wp::Vector2> const& points) {
  float area = 0.0f;
  for (size_t i = 0; i < points.size(); ++i) {
    auto const& a = points[i];
    auto const& b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area;
}

float ringArea(wp::geometry::Mesh const& mesh, uint32_t polygonIndex) {
  vector<wp::Vector2> points;
  for (auto vertex : mesh.getPolygon(polygonIndex).getOrderedVertexIndices()) {
    points.push_back(mesh.getVertex(vertex).getPosition());
  }
  return abs(twiceSignedArea(points));
}

uint32_t innermostRingAt(
    wp::geometry::Mesh const& mesh,
    vector<bw::core::MeshPrimitiveEditingProxy::NodeMapping> const& mappings,
    wp::Vector2 const& position) {
  auto depthOf = [&](uint32_t polygonIndex) {
    uint32_t depth = 0;
    for (;;) {
      auto mapping = ranges::find_if(mappings, [&](auto const& candidate) {
        return candidate.polygonIndex == polygonIndex;
      });
      if (mapping == mappings.end() || mapping->parentPolygonIndex == ~0u) return depth;
      polygonIndex = mapping->parentPolygonIndex;
      ++depth;
    }
  };

  uint32_t result = ~0u;
  uint32_t deepest = 0;
  float smallestArea = numeric_limits<float>::max();
  for (auto index = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(index);
       index = mesh.getNextPolygonIndex(index)) {
    auto area = ringArea(mesh, index);
    auto depth = depthOf(index);
    if (pointInsideRing(mesh, mesh.getPolygon(index), position) &&
        (area < smallestArea || (area == smallestArea && depth > deepest))) {
      result = index;
      deepest = depth;
      smallestArea = area;
    }
  }
  return result;
}

float orientation(wp::Vector2 const& a, wp::Vector2 const& b,
                  wp::Vector2 const& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointOnSegment(wp::Vector2 const& a, wp::Vector2 const& b,
                    wp::Vector2 const& p) {
  constexpr float epsilon = 0.0001f;
  return abs(orientation(a, b, p)) <= epsilon &&
         p.x >= min(a.x, b.x) - epsilon && p.x <= max(a.x, b.x) + epsilon &&
         p.y >= min(a.y, b.y) - epsilon && p.y <= max(a.y, b.y) + epsilon;
}

bool segmentsIntersect(wp::Vector2 const& a, wp::Vector2 const& b,
                       wp::Vector2 const& c, wp::Vector2 const& d) {
  auto o1 = orientation(a, b, c), o2 = orientation(a, b, d);
  auto o3 = orientation(c, d, a), o4 = orientation(c, d, b);
  if (((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) &&
      ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f))) return true;
  return pointOnSegment(a, b, c) || pointOnSegment(a, b, d) ||
         pointOnSegment(c, d, a) || pointOnSegment(c, d, b);
}

bool properSegmentsIntersect(wp::Vector2 const& a, wp::Vector2 const& b,
                             wp::Vector2 const& c, wp::Vector2 const& d) {
  auto o1 = orientation(a, b, c), o2 = orientation(a, b, d);
  auto o3 = orientation(c, d, a), o4 = orientation(c, d, b);
  return ((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) &&
         ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f));
}

bool segmentProperlyCrossesMesh(wp::geometry::Mesh const& mesh,
                                wp::Vector2 const& first,
                                wp::Vector2 const& second) {
  for (auto edgeIndex = mesh.getFirstEdgeIndex();
       !mesh.edgeIndexIterationFinished(edgeIndex);
       edgeIndex = mesh.getNextEdgeIndex(edgeIndex)) {
    auto const& edge = mesh.getEdge(edgeIndex);
    if (properSegmentsIntersect(
            first, second,
            mesh.getVertex(edge.getFirstVertex()).getPosition(),
            mesh.getVertex(edge.getSecondVertex()).getPosition())) return true;
  }
  return false;
}

uint32_t addDrawnRing(wp::geometry::Mesh& mesh,
                      vector<wp::Vector2> const& points) {
  wp::geometry::IndexVector vertices;
  wp::geometry::IndexVector edgeData;
  for (auto const& point : points) {
    vertices.push_back(mesh.addVertex(wp::geometry::Vertex(point)));
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto first = vertices[i], second = vertices[(i + 1) % vertices.size()];
    auto edge = mesh.addEdge(wp::geometry::Edge(first, second));
    edgeData.insert(edgeData.end(), {first, second, edge});
  }
  return mesh.addPolygon(wp::geometry::Polygon(edgeData));
}

}  // namespace editor::meshGeometry
