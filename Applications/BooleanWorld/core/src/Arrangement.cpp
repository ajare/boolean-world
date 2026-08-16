#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <mapbox/earcut.hpp>

#include <core/Arrangement.h>
#include <core/ClipperDefines.h>

namespace expr {
using namespace Clipper2Lib;
using namespace std;

Membership::Membership(size_t primitiveCount)
    : mWords((primitiveCount + 63) / 64) {
}

void Membership::set(size_t primitiveIndex, bool value) {
  auto wordIndex = primitiveIndex / 64;
  auto mask = uint64_t(1) << (primitiveIndex % 64);
  if (value) {
    mWords[wordIndex] |= mask;
  } else {
    mWords[wordIndex] &= ~mask;
  }
}

bool Membership::contains(size_t primitiveIndex) const {
  auto wordIndex = primitiveIndex / 64;
  return wordIndex < mWords.size() &&
         (mWords[wordIndex] & (uint64_t(1) << (primitiveIndex % 64))) != 0;
}

bool EvaluateFold(vector<ArrangementPrimitive> const& primitives, Membership const& membership) {
  vector<size_t> priorityOrder(primitives.size());
  for (size_t i = 0; i < priorityOrder.size(); ++i) {
    priorityOrder[i] = i;
  }
  stable_sort(priorityOrder.begin(), priorityOrder.end(), [&](size_t lhs, size_t rhs) {
    return primitives[lhs].priority < primitives[rhs].priority;
  });

  bool inside = false;
  for (auto primitiveIndex : priorityOrder) {
    auto member = membership.contains(primitiveIndex);
    switch (primitives[primitiveIndex].operation) {
      case bw::core::Primitive::Operation::Union:
        inside = inside || member;
        break;
      case bw::core::Primitive::Operation::Intersection:
        inside = inside && member;
        break;
      case bw::core::Primitive::Operation::Difference:
        inside = inside && !member;
        break;
      case bw::core::Primitive::Operation::XOR:
        inside = inside != member;
        break;
    }
  }
  return inside;
}

namespace {
struct Segment {
  Vertex v[2];
  uint32_t primitiveIndex;
};

struct RationalPoint {
  int64_t xNumerator;
  int64_t yNumerator;
  int64_t denominator;
};

struct Box {
  int64_t minx;
  int64_t miny;
  int64_t maxx;
  int64_t maxy;
};

struct PointHash {
  size_t operator()(Vertex const& v) const {
    auto x = hash<int64_t>()(v.x);
    auto y = hash<int64_t>()(v.y);
    return x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2));
  }
};

int64_t CrossVector(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
  return ax * by - ay * bx;
}

int64_t Cross(Vertex const& a, Vertex const& b, Vertex const& c) {
  return CrossVector(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y);
}

int DifferenceOfProductsSign(int64_t a, int64_t b, int64_t c, int64_t d) {
#ifdef _MSC_VER
  int64_t lhsHigh;
  int64_t rhsHigh;
  auto lhsLow = uint64_t(_mul128(a, b, &lhsHigh));
  auto rhsLow = uint64_t(_mul128(c, d, &rhsHigh));
  auto low = lhsLow - rhsLow;
  auto high = uint64_t(lhsHigh) - uint64_t(rhsHigh) - uint64_t(lhsLow < rhsLow);
  return (high >> 63) != 0 ? -1 : (high > 0 || low > 0 ? 1 : 0);
#else
  using int128 = __int128;
  auto result = int128(a) * int128(b) - int128(c) * int128(d);
  return (result > 0) - (result < 0);
#endif
}

int CrossSign(Vertex const& a, Vertex const& b, RationalPoint const& point) {
  auto relativeX = point.xNumerator - a.x * point.denominator;
  auto relativeY = point.yNumerator - a.y * point.denominator;
  return DifferenceOfProductsSign(
      b.x - a.x,
      relativeY,
      b.y - a.y,
      relativeX);
}

uint64_t UnsignedMagnitude(int64_t value) {
  return value < 0 ? uint64_t(-(value + 1)) + 1 : uint64_t(value);
}

// Returns round(base + numerator * multiplier / denominator) without
// overflowing a 64-bit intermediate. Half-grid ties are rounded away from
// zero, independently of segment direction or pair ordering.
int64_t RoundedCoordinate(int64_t base, int64_t numerator, int64_t multiplier, int64_t denominator) {
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }

  int64_t quotient;
  int64_t remainder;
#ifdef _MSC_VER
  int64_t high;
  auto low = _mul128(numerator, multiplier, &high);
  quotient = _div128(high, low, denominator, &remainder);
#else
  using int128 = __int128;
  auto product = int128(numerator) * int128(multiplier);
  quotient = int64_t(product / denominator);
  remainder = int64_t(product % denominator);
#endif

  auto result = base + quotient;
  auto remainderMagnitude = UnsignedMagnitude(remainder);
  auto denominatorMagnitude = uint64_t(denominator);
  auto beyondHalf = remainderMagnitude > denominatorMagnitude - remainderMagnitude;
  auto atHalf = remainderMagnitude == denominatorMagnitude - remainderMagnitude;
  auto remainderSign = (remainder > 0) - (remainder < 0);

  if (beyondHalf ||
      (atHalf && (result == 0 || (result > 0) == (remainder > 0)))) {
    result += remainderSign;
  }
  return result;
}

vector<Segment> ExtractSegments(vector<bw::core::Clipper2Polygon> const& polygons) {
  vector<Segment> result;

  for (int i = 0; i < int(polygons.size()); ++i) {
    auto const& path = polygons[i].path;
    if (path.size() < 2) {
      continue;
    }

    for (size_t j = 0; j < path.size(); ++j) {
      auto k = (j + 1) % path.size();
      Vertex a{path[j].x, path[j].y};
      Vertex b{path[k].x, path[k].y};
      if (a != b) {
        result.push_back({{a, b}, polygons[i].primitiveIndex});
      }
    }
  }

  return result;
}

bool PointOnSegment(Vertex const& point, Vertex const& a, Vertex const& b) {
  return Cross(a, b, point) == 0 &&
         point.x >= min(a.x, b.x) && point.x <= max(a.x, b.x) &&
         point.y >= min(a.y, b.y) && point.y <= max(a.y, b.y);
}

struct Intersection {
  bool hit{false};
  Vertex point{};
};

Intersection SegmentIntersection(Segment const& subject, Segment const& clip) {
  auto const& p = subject.v[0];
  auto const& q = clip.v[0];
  auto rx = subject.v[1].x - p.x;
  auto ry = subject.v[1].y - p.y;
  auto sx = clip.v[1].x - q.x;
  auto sy = clip.v[1].y - q.y;
  auto denominator = CrossVector(rx, ry, sx, sy);

  if (denominator == 0) {
    return {};
  }

  auto qpx = q.x - p.x;
  auto qpy = q.y - p.y;
  auto subjectNumerator = CrossVector(qpx, qpy, sx, sy);
  auto clipNumerator = CrossVector(qpx, qpy, rx, ry);

  if (denominator > 0) {
    if (subjectNumerator < 0 || subjectNumerator > denominator ||
        clipNumerator < 0 || clipNumerator > denominator) {
      return {};
    }
  } else if (subjectNumerator > 0 || subjectNumerator < denominator ||
             clipNumerator > 0 || clipNumerator < denominator) {
    return {};
  }

  return {
      true,
      {RoundedCoordinate(p.x, subjectNumerator, rx, denominator),
       RoundedCoordinate(p.y, subjectNumerator, ry, denominator)}};
}

bool VertexLessAlongSegment(Vertex const& lhs, Vertex const& rhs, Segment const& segment) {
  auto dx = segment.v[1].x - segment.v[0].x;
  auto dy = segment.v[1].y - segment.v[0].y;
  if (abs(dx) >= abs(dy)) {
    if (lhs.x != rhs.x) {
      return dx >= 0 ? lhs.x < rhs.x : lhs.x > rhs.x;
    }
    return dy >= 0 ? lhs.y < rhs.y : lhs.y > rhs.y;
  }
  if (lhs.y != rhs.y) {
    return dy >= 0 ? lhs.y < rhs.y : lhs.y > rhs.y;
  }
  return dx >= 0 ? lhs.x < rhs.x : lhs.x > rhs.x;
}

int64_t SignedArea2(PSLG const& graph, vector<int> const& vertices) {
  int64_t area = 0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& a = graph.vs[vertices[i]];
    auto const& b = graph.vs[vertices[(i + 1) % vertices.size()]];
    area += a.x * b.y - a.y * b.x;
  }
  return area;
}

RationalPoint SamplePoint(PSLG const& graph, Cycle const& cycle) {
  auto const& p0 = graph.vs[cycle.vis[0]];
  auto const& p1 = graph.vs[cycle.vis[1]];
  auto dx = p1.x - p0.x;
  auto dy = p1.y - p0.y;
  auto scale = max<int64_t>(1, max(abs(dx), abs(dy)));
  auto denominator = 4 * scale;
  return {
      2 * scale * (p0.x + p1.x) - dy,
      2 * scale * (p0.y + p1.y) + dx,
      denominator};
}

int PointInCycle(RationalPoint const& point, Cycle const& cycle, PSLG const& graph) {
  int winding = 0;
  for (size_t i = 0; i < cycle.vis.size(); ++i) {
    auto const& a = graph.vs[cycle.vis[i]];
    auto const& b = graph.vs[cycle.vis[(i + 1) % cycle.vis.size()]];
    auto aBelow = a.y * point.denominator <= point.yNumerator;
    auto bBelow = b.y * point.denominator <= point.yNumerator;
    auto cross = CrossSign(a, b, point);

    if (aBelow) {
      if (!bBelow && cross > 0) {
        ++winding;
      }
    } else if (bBelow && cross < 0) {
      --winding;
    }
  }
  return winding == 0 ? 0 : 1;
}

int PointInCycle(Vertex const& point, Cycle const& cycle, PSLG const& graph) {
  int winding = 0;
  for (size_t i = 0; i < cycle.vis.size(); ++i) {
    auto const& a = graph.vs[cycle.vis[i]];
    auto const& b = graph.vs[cycle.vis[(i + 1) % cycle.vis.size()]];
    if (PointOnSegment(point, a, b)) {
      return -1;
    }
    auto cross = Cross(a, b, point);
    if (a.y <= point.y) {
      if (b.y > point.y && cross > 0) {
        ++winding;
      }
    } else if (b.y <= point.y && cross < 0) {
      --winding;
    }
  }
  return winding == 0 ? 0 : 1;
}

int PointInPolygon(RationalPoint const& point, Path64 const& polygon) {
  int winding = 0;
  for (size_t i = 0; i < polygon.size(); ++i) {
    Vertex a{polygon[i].x, polygon[i].y};
    Vertex b{polygon[(i + 1) % polygon.size()].x, polygon[(i + 1) % polygon.size()].y};
    auto aBelow = a.y * point.denominator <= point.yNumerator;
    auto bBelow = b.y * point.denominator <= point.yNumerator;
    auto cross = CrossSign(a, b, point);
    if (aBelow) {
      if (!bBelow && cross > 0) {
        ++winding;
      }
    } else if (bBelow && cross < 0) {
      --winding;
    }
  }
  return winding;
}

Box GetPathBounds(Path64 const& path) {
  Box bounds{path[0].x, path[0].y, path[0].x, path[0].y};
  for (auto const& point : path) {
    bounds.minx = min(bounds.minx, point.x);
    bounds.miny = min(bounds.miny, point.y);
    bounds.maxx = max(bounds.maxx, point.x);
    bounds.maxy = max(bounds.maxy, point.y);
  }
  return bounds;
}

Box GetBounds(PSLG const& graph, Cycle const& cycle) {
  auto const& first = graph.vs[cycle.vis[0]];
  Box bounds{first.x, first.y, first.x, first.y};
  for (auto vertexIndex : cycle.vis) {
    auto const& vertex = graph.vs[vertexIndex];
    bounds.minx = min(bounds.minx, vertex.x);
    bounds.miny = min(bounds.miny, vertex.y);
    bounds.maxx = max(bounds.maxx, vertex.x);
    bounds.maxy = max(bounds.maxy, vertex.y);
  }
  return bounds;
}

bool ContainsBox(Box const& outer, Box const& inner) {
  return outer.minx < inner.minx && outer.maxx > inner.maxx &&
         outer.miny < inner.miny && outer.maxy > inner.maxy;
}

bool EqualBox(Box const& lhs, Box const& rhs) {
  return lhs.minx == rhs.minx && lhs.miny == rhs.miny &&
         lhs.maxx == rhs.maxx && lhs.maxy == rhs.maxy;
}

bool IsLeafSolidBoundaryInsideSolid(PSLG const& graph, Cycle const& cycle) {
  if (cycle.area >= 0) {
    return false;
  }

  auto cycleBounds = GetBounds(graph, cycle);
  for (size_t i = 0; i < graph.sourceContours.size(); ++i) {
    if (graph.sourceContourIsHole[i] || graph.sourceContours[i].empty() ||
        !EqualBox(cycleBounds, GetPathBounds(graph.sourceContours[i]))) {
      continue;
    }

    bool containsContour = false;
    int nearestContainer = -1;
    int64_t nearestArea = numeric_limits<int64_t>::max();
    for (size_t j = 0; j < graph.sourceContours.size(); ++j) {
      if (i == j || graph.sourceContours[j].empty()) {
        continue;
      }
      auto otherBounds = GetPathBounds(graph.sourceContours[j]);
      if (ContainsBox(otherBounds, cycleBounds)) {
        auto area = (otherBounds.maxx - otherBounds.minx) *
                    (otherBounds.maxy - otherBounds.miny);
        if (area < nearestArea) {
          nearestArea = area;
          nearestContainer = int(j);
        }
      }
      if (ContainsBox(cycleBounds, otherBounds)) {
        containsContour = true;
      }
    }
    return nearestContainer >= 0 &&
           !graph.sourceContourIsHole[nearestContainer] &&
           !containsContour;
  }
  return false;
}
}  // namespace

PSLG BuildPSLG(vector<bw::core::Clipper2Polygon> const& polygons, vector<bw::core::Primitive*> const& primitives) {
  (void)primitives;
  auto segments = ExtractSegments(polygons);
  vector<vector<Vertex>> splits(segments.size());
  vector<Vertex> candidates;

  for (size_t i = 0; i < segments.size(); ++i) {
    splits[i].push_back(segments[i].v[0]);
    splits[i].push_back(segments[i].v[1]);
    candidates.push_back(segments[i].v[0]);
    candidates.push_back(segments[i].v[1]);
  }

  // Predicates are exact and each computed intersection is snapped immediately.
  for (size_t i = 0; i < segments.size(); ++i) {
    for (size_t j = i + 1; j < segments.size(); ++j) {
      auto intersection = SegmentIntersection(segments[i], segments[j]);
      if (!intersection.hit) {
        continue;
      }
      splits[i].push_back(intersection.point);
      splits[j].push_back(intersection.point);
      candidates.push_back(intersection.point);
    }
  }

  // Snapping can create a new incidence on an edge that was not part of the
  // original intersection. Re-check every snapped point against every edge.
  for (auto const& candidate : candidates) {
    for (size_t i = 0; i < segments.size(); ++i) {
      if (PointOnSegment(candidate, segments[i].v[0], segments[i].v[1])) {
        splits[i].push_back(candidate);
      }
    }
  }

  PSLG graph;
  for (auto const& polygon : polygons) {
    graph.sourceContours.push_back(polygon.path);
    graph.sourceContourIsHole.push_back(polygon.isHole);
  }

  unordered_map<Vertex, int, PointHash> vertexMap;
  map<pair<int, int>, int> edgeMap;

  auto getVertex = [&](Vertex const& vertex) {
    auto [it, inserted] = vertexMap.emplace(vertex, int(graph.vs.size()));
    if (inserted) {
      graph.vs.push_back(vertex);
    }
    return it->second;
  };

  for (size_t i = 0; i < segments.size(); ++i) {
    auto& points = splits[i];
    sort(points.begin(), points.end(), [&](Vertex const& lhs, Vertex const& rhs) {
      return VertexLessAlongSegment(lhs, rhs, segments[i]);
    });
    points.erase(unique(points.begin(), points.end()), points.end());

    for (size_t j = 0; j + 1 < points.size(); ++j) {
      if (points[j] == points[j + 1]) {
        continue;
      }
      auto directedStart = getVertex(points[j]);
      auto directedEnd = getVertex(points[j + 1]);
      auto a = directedStart;
      auto b = directedEnd;
      if (a > b) {
        swap(a, b);
      }

      auto [edgeIt, inserted] = edgeMap.emplace(
          make_pair(a, b), int(graph.es.size()));
      if (inserted) {
        graph.es.push_back({a, b});
      }

      if (segments[i].primitiveIndex != ~0u) {
        auto& edge = graph.es[edgeIt->second];
        auto contribution = find_if(
            edge.windingDeltas.begin(), edge.windingDeltas.end(),
            [&](WindingDelta const& value) {
              return value.primitiveIndex == segments[i].primitiveIndex;
            });
        auto delta = directedStart == edge.vi[0] ? 1 : -1;
        if (contribution == edge.windingDeltas.end()) {
          edge.windingDeltas.push_back({segments[i].primitiveIndex, delta});
        } else {
          contribution->delta += delta;
        }
      }
    }
  }

  return graph;
}

vector<Cycle> ExtractMinimalCycles(PSLG const& graph) {
  struct HalfEdge {
    int v[2];
    int twin;
    int edge;
    int next{-1};
    bool visited{false};
  };

  vector<HalfEdge> halfEdges;
  vector<vector<int>> outgoing(graph.vs.size());
  halfEdges.reserve(graph.es.size() * 2);

  for (int i = 0; i < int(graph.es.size()); ++i) {
    auto const& edge = graph.es[i];
    auto h = int(halfEdges.size());
    halfEdges.push_back({{edge.vi[0], edge.vi[1]}, h + 1, i});
    halfEdges.push_back({{edge.vi[1], edge.vi[0]}, h, i});
    outgoing[edge.vi[0]].push_back(h);
    outgoing[edge.vi[1]].push_back(h + 1);
  }

  // Exact polar ordering keeps face adjacency independent of floating point.
  for (auto& edges : outgoing) {
    sort(edges.begin(), edges.end(), [&](int lhsIndex, int rhsIndex) {
      auto const& lhs = halfEdges[lhsIndex];
      auto const& rhs = halfEdges[rhsIndex];
      auto const& origin = graph.vs[lhs.v[0]];
      auto const& lhsEnd = graph.vs[lhs.v[1]];
      auto const& rhsEnd = graph.vs[rhs.v[1]];
      auto ldx = lhsEnd.x - origin.x;
      auto ldy = lhsEnd.y - origin.y;
      auto rdx = rhsEnd.x - origin.x;
      auto rdy = rhsEnd.y - origin.y;
      auto lhsUpper = ldy > 0 || (ldy == 0 && ldx >= 0);
      auto rhsUpper = rdy > 0 || (rdy == 0 && rdx >= 0);
      if (lhsUpper != rhsUpper) {
        return lhsUpper > rhsUpper;
      }
      auto cross = CrossVector(ldx, ldy, rdx, rdy);
      if (cross != 0) {
        return cross > 0;
      }
      return lhs.edge < rhs.edge;
    });
  }

  for (auto& halfEdge : halfEdges) {
    auto& edges = outgoing[halfEdges[halfEdge.twin].v[0]];
    auto twin = find(edges.begin(), edges.end(), halfEdge.twin);
    auto index = int(distance(edges.begin(), twin));
    halfEdge.next = edges[(index - 1 + int(edges.size())) % int(edges.size())];
  }

  vector<Cycle> cycles;
  for (int start = 0; start < int(halfEdges.size()); ++start) {
    if (halfEdges[start].visited) {
      continue;
    }

    Cycle cycle;
    auto current = start;
    do {
      halfEdges[current].visited = true;
      cycle.vis.push_back(halfEdges[current].v[0]);
      cycle.eis.push_back(halfEdges[current].edge);
      current = halfEdges[current].next;
    } while (current != start);

    if (cycle.vis.size() < 3) {
      continue;
    }
    cycle.area = SignedArea2(graph, cycle.vis);
    // Zero-area walks are construction debris and clockwise walks normally
    // bound the unbounded exterior. A nested leaf solid is the exception: both
    // sides are bounded faces and its clockwise boundary separates them.
    if (cycle.area > 0 || IsLeafSolidBoundaryInsideSolid(graph, cycle)) {
      cycles.push_back(move(cycle));
    }
  }

  return cycles;
}

bool PointInFace(Vertex const& vertex, Face const& face, vector<Cycle> const& cycles, PSLG const& graph) {
  if (PointInCycle(vertex, cycles[face.polygon], graph) <= 0) {
    return false;
  }
  for (auto hole : face.holes) {
    if (PointInCycle(vertex, cycles[hole], graph) > 0) {
      return false;
    }
  }
  return true;
}

vector<PolygonNode> BuildPolygonHierarchy(PSLG const& graph, vector<Cycle>& cycles) {
  vector<PolygonNode> nodes(cycles.size());
  vector<Box> boxes(cycles.size());
  for (int i = 0; i < int(cycles.size()); ++i) {
    nodes[i].cycleIndex = i;
    boxes[i] = GetBounds(graph, cycles[i]);
  }

  for (int i = 0; i < int(cycles.size()); ++i) {
    auto sample = SamplePoint(graph, cycles[i]);
    auto bestArea = numeric_limits<int64_t>::max();
    auto bestParent = -1;
    for (int j = 0; j < int(cycles.size()); ++j) {
      if (i == j || !ContainsBox(boxes[j], boxes[i]) ||
          !PointInCycle(sample, cycles[j], graph)) {
        continue;
      }
      auto area = abs(cycles[j].area);
      if (area < bestArea) {
        bestArea = area;
        bestParent = j;
      }
    }
    nodes[i].parent = bestParent;
  }

  for (int i = 0; i < int(nodes.size()); ++i) {
    if (nodes[i].parent >= 0) {
      nodes[nodes[i].parent].children.push_back(i);
    }
  }
  return nodes;
}

vector<Face> BuildFaces(vector<PolygonNode> const& nodes, vector<Cycle> const& cycles) {
  (void)cycles;
  vector<Face> faces;
  for (auto const& node : nodes) {
    Face face;
    face.polygon = node.cycleIndex;
    for (auto child : node.children) {
      face.holes.push_back(nodes[child].cycleIndex);
    }
    faces.push_back(move(face));
  }
  return faces;
}

vector<Face> CalculateOwningPolygons(vector<Face> const& faces, vector<bw::core::Clipper2Polygon> const& polygons, vector<Cycle> const& cycles, PSLG& graph, vector<bw::core::Primitive*> const& primitives) {
  vector<Face> keptFaces;
  vector<int> removedFaceIndices;

  for (int i = 0; i < int(faces.size()); ++i) {
    auto const& cycle = cycles[faces[i].polygon];
    auto sample = SamplePoint(graph, cycle);
    auto face = faces[i];
    int previousOwner{-1};

    for (int j = 0; j < int(polygons.size()); ++j) {
      if (!PointInPolygon(sample, polygons[j].path)) {
        continue;
      }
      auto primitive = primitives[polygons[j].primitiveIndex];
      if (primitive->getOperation() == bw::core::Primitive::Operation::Difference) {
        face.holePolygon = j;
        face.owningPolygon = previousOwner;
      } else {
        previousOwner = face.owningPolygon;
        face.owningPolygon = j;
      }
    }

    if (face.owningPolygon < 0) {
      removedFaceIndices.push_back(i);
      continue;
    }

    for (auto edgeIndex : cycle.eis) {
      auto& edge = graph.es[edgeIndex];
      (edge.fi[0] < 0 ? edge.fi[0] : edge.fi[1]) = i;
    }
    for (auto hole : face.holes) {
      for (auto edgeIndex : cycles[hole].eis) {
        auto& edge = graph.es[edgeIndex];
        (edge.fi[0] < 0 ? edge.fi[0] : edge.fi[1]) = i;
      }
    }

    auto primitive = primitives[polygons[face.owningPolygon].primitiveIndex];
    if (primitive->getOperation() != bw::core::Primitive::Operation::Difference) {
      keptFaces.push_back(move(face));
    }
  }

  for (auto faceIndex : removedFaceIndices) {
    for (auto edgeIndex : cycles[faces[faceIndex].polygon].eis) {
      graph.es[edgeIndex].fi[0] = faceIndex;
      graph.es[edgeIndex].fi[1] = -1;
    }
  }
  return keptFaces;
}

ArrangementResult BuildArrangement(vector<ArrangementPrimitive> const& primitives) {
  vector<bw::core::Clipper2Polygon> contours;
  for (uint32_t primitiveIndex = 0;
       primitiveIndex < uint32_t(primitives.size()); ++primitiveIndex) {
    for (auto const& contour : primitives[primitiveIndex].contours) {
      contours.push_back({false, primitiveIndex, contour});
    }
  }

  ArrangementResult result;
  result.graph = BuildPSLG(contours, {});
  result.cycles = ExtractMinimalCycles(result.graph);
  erase_if(result.cycles, [](Cycle const& cycle) {
    return cycle.area <= 0;
  });
  result.hierarchy = BuildPolygonHierarchy(result.graph, result.cycles);
  result.faces = BuildFaces(result.hierarchy, result.cycles);

  auto assignCycleSide = [&](int faceIndex, int cycleIndex, bool assignLeft) {
    auto const& cycle = result.cycles[cycleIndex];
    for (size_t i = 0; i < cycle.eis.size(); ++i) {
      auto& edge = result.graph.es[cycle.eis[i]];
      auto traversalMatchesEdge = edge.vi[0] == cycle.vis[i];
      auto leftSide = traversalMatchesEdge ? 0 : 1;
      auto side = assignLeft ? leftSide : 1 - leftSide;
      edge.fi[side] = faceIndex;
    }
  };

  for (int faceIndex = 0; faceIndex < int(result.faces.size()); ++faceIndex) {
    auto const& face = result.faces[faceIndex];
    assignCycleSide(faceIndex, face.polygon, true);
    for (auto hole : face.holes) {
      assignCycleSide(faceIndex, hole, false);
    }
  }

  vector<vector<int>> faceEdges(result.faces.size());
  for (int edgeIndex = 0; edgeIndex < int(result.graph.es.size()); ++edgeIndex) {
    auto const& edge = result.graph.es[edgeIndex];
    for (auto faceIndex : edge.fi) {
      if (faceIndex >= 0) {
        faceEdges[faceIndex].push_back(edgeIndex);
      }
    }
  }

  vector<vector<int32_t>> windingNumbers(
      result.faces.size(), vector<int32_t>(primitives.size()));
  vector<bool> visited(result.faces.size(), false);
  queue<int> pending;

  for (int seedFace = 0; seedFace < int(result.faces.size()); ++seedFace) {
    if (visited[seedFace]) {
      continue;
    }

    auto sample = SamplePoint(
        result.graph, result.cycles[result.faces[seedFace].polygon]);
    for (size_t primitiveIndex = 0;
         primitiveIndex < primitives.size(); ++primitiveIndex) {
      for (auto const& contour : primitives[primitiveIndex].contours) {
        windingNumbers[seedFace][primitiveIndex] +=
            PointInPolygon(sample, contour);
      }
    }

    visited[seedFace] = true;
    pending.push(seedFace);
    while (!pending.empty()) {
      auto faceIndex = pending.front();
      pending.pop();

      for (auto edgeIndex : faceEdges[faceIndex]) {
        auto const& edge = result.graph.es[edgeIndex];
        if (!edge.doubleSided()) {
          continue;
        }
        auto neighbour = edge.fi[0] == faceIndex ? edge.fi[1] : edge.fi[0];
        if (visited[neighbour]) {
          continue;
        }

        windingNumbers[neighbour] = windingNumbers[faceIndex];
        auto direction = edge.fi[1] == faceIndex ? 1 : -1;
        for (auto const& delta : edge.windingDeltas) {
          windingNumbers[neighbour][delta.primitiveIndex] +=
              direction * delta.delta;
        }
        visited[neighbour] = true;
        pending.push(neighbour);
      }
    }
  }

  for (size_t faceIndex = 0; faceIndex < result.faces.size(); ++faceIndex) {
    Membership membership(primitives.size());
    for (size_t primitiveIndex = 0;
         primitiveIndex < primitives.size(); ++primitiveIndex) {
      auto winding = windingNumbers[faceIndex][primitiveIndex];
      auto member = primitives[primitiveIndex].fillRule ==
                            bw::core::Primitive::FillRule::EvenOdd
                        ? abs(winding) % 2 == 1
                        : winding != 0;
      membership.set(primitiveIndex, member);
    }
    result.faces[faceIndex].membership = move(membership);
    result.faces[faceIndex].solid = EvaluateFold(
        primitives, result.faces[faceIndex].membership);
  }

  return result;
}

vector<FaceTriangle> BuildFaceTriangles(vector<Face> const& faces, vector<Cycle> const& cycles, PSLG const& graph) {
  using EarcutPoint = array<float, 2>;
  vector<FaceTriangle> triangles;

  for (int i = 0; i < int(faces.size()); ++i) {
    vector<vector<EarcutPoint>> polygons;
    vector<int> vertexIndices;

    auto addCycle = [&](int cycleIndex) {
      vector<EarcutPoint> polygon;
      for (auto vertexIndex : cycles[cycleIndex].vis) {
        auto const& vertex = graph.vs[vertexIndex];
        polygon.push_back({float(vertex.x / BW_CLIPPER_SCALE), float(vertex.y / BW_CLIPPER_SCALE)});
        vertexIndices.push_back(vertexIndex);
      }
      polygons.push_back(move(polygon));
    };

    addCycle(faces[i].polygon);
    for (auto hole : faces[i].holes) {
      addCycle(hole);
    }

    auto indices = mapbox::earcut<uint32_t>(polygons);
    for (size_t j = 0; j < indices.size(); j += 3) {
      triangles.push_back({{vertexIndices[indices[j]], vertexIndices[indices[j + 1]], vertexIndices[indices[j + 2]]},
                           i});
    }
  }
  return triangles;
}
}  // namespace expr
