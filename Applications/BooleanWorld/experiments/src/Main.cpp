#include <iostream>
#include <filesystem>
#include <random>

#include <gtest/gtest.h>

#include <willpower/common/Timer.h>

#include <core/YamlSerializer.h>
#include <core/World.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/Clipper2Polygon.h>

using namespace std;

shared_ptr<bw::core::World> createWorld(float size, float gridSize) {
  auto world = make_shared<bw::core::World>(size, gridSize);

  auto genFn = [world](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
    auto wdg = new bw::core::DynamicWorldDataGenerator(world.get());

    wdg->setBroadPhaseCulling(bw::core::WorldDataGenerator::BroadPhaseCulling::None);
    wdg->setNarrowPhaseCulling(bw::core::WorldDataGenerator::NarrowPhaseCulling::None);

    wdg->setAlwaysUpdateVertices(true);
    wdg->setAllowCommitIfVisible(true);

    return wdg;
  };

  world->setWorldDataGeneratorFactory(genFn);

  return world;
}

shared_ptr<bw::core::World> openWorld(string const& filepath) {
  auto path = filesystem::path(filepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  shared_ptr<bw::core::World> world;

  if (ext == ".yaml") {
    auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      cout << e.what() << "\n";
      return nullptr;
    }

    world = createWorld(8192, 8192);

    auto workData = bw::core::SerializationWorkData{};

    if (world->deserialize(ser, workData)) {
      auto const& warnings = world->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          cout << warning << "\n";
        }
      }

      return world;
    } else {
      auto const& errors = world->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          cout << error << "\n";
        }
      }

      return nullptr;
    }
  } else {
    cout << "Unsupported file format.\n";
    return nullptr;
  }
}

////////////////////////////////////////////////////////////////
// Geometry

#include <clipper2/clipper.h>

#include <vector>
#include <unordered_map>
#include <set>
#include <cmath>
#include <algorithm>

namespace expr {
using namespace Clipper2Lib;

struct Vertex {
  Point64 p;
};

struct Edge {
  int v0;
  int v1;
};

struct HalfEdge {
  int origin;
  int dest;

  int twin;

  bool visited = false;
};

enum struct FaceType {
  Unknown,
  Regular,
  Hole,
  Island,
  Unbounded
};

struct Face {
  std::vector<int> vertices;
  double area;
  std::vector<uint32_t> primitiveIndices;
  Point64 interiorPoint{};
  bool filled = false;
  int winding = 0;
  FaceType type{FaceType::Unknown};
};

struct PSLG {
  std::vector<Vertex> vertices;
  std::vector<Edge> edges;
};

struct Segment {
  Point64 a;
  Point64 b;
};

struct PointHash {
  size_t operator()(const Point64& p) const {
    uint64_t x = static_cast<uint64_t>(p.x);
    uint64_t y = static_cast<uint64_t>(p.y);

    return std::hash<uint64_t>()(
        x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2)));
  }
};

struct PointEq {
  bool operator()(const Point64& a,
                  const Point64& b) const {
    return a.x == b.x && a.y == b.y;
  }
};

std::vector<Segment>
ExtractSegments(std::vector<bw::core::Clipper2Polygon> const& polygons) {
  std::vector<Segment> result;

  for (auto const& polygon : polygons) {
    auto const& path = polygon.path;

    if (path.size() < 2)
      continue;

    for (size_t i = 0; i < path.size(); ++i) {
      result.push_back(
          {path[i],
           path[(i + 1) % path.size()]});
    }
  }

  return result;
}

struct Intersection {
  bool hit;
  double t0;
  double t1;
  Point64 p;
};

struct Overlap {
  bool hit = false;
  Point64 a;
  Point64 b;
};

static inline int64_t Cross(
    const Point64& a,
    const Point64& b,
    const Point64& c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

static inline bool Collinear(
    const Point64& a,
    const Point64& b,
    const Point64& c) {
  return Cross(a, b, c) == 0;
}

Overlap CollinearOverlap(
    const Segment& s0,
    const Segment& s1) {
  if (!Collinear(s0.a, s0.b, s1.a))
    return {false, Point64(), Point64()};

  if (!Collinear(s0.a, s0.b, s1.b))
    return {false, Point64(), Point64()};

  bool useX =
      std::abs(s0.b.x - s0.a.x) >=
      std::abs(s0.b.y - s0.a.y);

  auto coord =
      [&](const Point64& p) {
        return useX ? p.x : p.y;
      };

  int64_t a0 = coord(s0.a);
  int64_t a1 = coord(s0.b);

  int64_t b0 = coord(s1.a);
  int64_t b1 = coord(s1.b);

  if (a0 > a1)
    std::swap(a0, a1);

  if (b0 > b1)
    std::swap(b0, b1);

  int64_t lo =
      std::max(a0, b0);

  int64_t hi =
      std::min(a1, b1);

  if (lo > hi)
    return {false, Point64(), Point64()};

  auto PointAtCoord =
      [&](const Segment& s,
          int64_t c) {
        int64_t sx =
            s.b.x - s.a.x;

        int64_t sy =
            s.b.y - s.a.y;

        if (useX) {
          double t =
              double(c - s.a.x) /
              double(sx);

          return Point64{
              c,
              (int64_t)std::llround(
                  s.a.y + t * sy)};
        } else {
          double t =
              double(c - s.a.y) /
              double(sy);

          return Point64{
              (int64_t)std::llround(
                  s.a.x + t * sx),
              c};
        }
      };

  return {
      true,
      PointAtCoord(s0, lo),
      PointAtCoord(s0, hi)};
}

double ParameterOnSegment(
    const Segment& s,
    const Point64& p) {
  int64_t dx = s.b.x - s.a.x;
  int64_t dy = s.b.y - s.a.y;

  if (std::abs(dx) >= std::abs(dy)) {
    if (dx == 0) return 0.0;

    return double(p.x - s.a.x) /
           double(dx);
  } else {
    if (dy == 0) return 0.0;

    return double(p.y - s.a.y) /
           double(dy);
  }
}

Intersection SegmentIntersection(
    const Segment& s0,
    const Segment& s1) {
  double x1 = (double)s0.a.x;
  double y1 = (double)s0.a.y;
  double x2 = (double)s0.b.x;
  double y2 = (double)s0.b.y;

  double x3 = (double)s1.a.x;
  double y3 = (double)s1.a.y;
  double x4 = (double)s1.b.x;
  double y4 = (double)s1.b.y;

  double dx1 = x2 - x1;
  double dy1 = y2 - y1;

  double dx2 = x4 - x3;
  double dy2 = y4 - y3;

  double denom = dx1 * dy2 - dy1 * dx2;

  if (std::abs(denom) < 1e-12)
    return {false, 0, 0, Point64()};

  double t =
      ((x3 - x1) * dy2 -
       (y3 - y1) * dx2) /
      denom;

  double u =
      ((x3 - x1) * dy1 -
       (y3 - y1) * dx1) /
      denom;

  if (t < 0.0 || t > 1.0)
    return {false, 0, 0, Point64()};

  if (u < 0.0 || u > 1.0)
    return {false, 0, 0, Point64()};

  Point64 p(
      (int64_t)std::llround(x1 + t * dx1),
      (int64_t)std::llround(y1 + t * dy1));

  return {true, t, u, p};
}

PSLG BuildPSLG(std::vector<bw::core::Clipper2Polygon> const& polygons) {
  std::vector<Segment> segs =
      ExtractSegments(polygons);

  size_t n = segs.size();

  struct SplitPoint {
    double t;
    Point64 p;
  };

  std::vector<std::vector<SplitPoint>>
      splits(n);

  for (size_t i = 0; i < n; ++i) {
    splits[i].push_back({0.0, segs[i].a});
    splits[i].push_back({1.0, segs[i].b});
  }

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      auto hit =
          SegmentIntersection(
              segs[i],
              segs[j]);

      if (hit.hit) {
        splits[i].push_back(
            {hit.t0, hit.p});

        splits[j].push_back(
            {hit.t1, hit.p});

        continue;
      }

      auto ov =
          CollinearOverlap(
              segs[i],
              segs[j]);

      if (!ov.hit)
        continue;

      double t0a =
          ParameterOnSegment(
              segs[i], ov.a);

      double t0b =
          ParameterOnSegment(
              segs[i], ov.b);

      double t1a =
          ParameterOnSegment(
              segs[j], ov.a);

      double t1b =
          ParameterOnSegment(
              segs[j], ov.b);

      splits[i].push_back(
          {t0a, ov.a});

      splits[i].push_back(
          {t0b, ov.b});

      splits[j].push_back(
          {t1a, ov.a});

      splits[j].push_back(
          {t1b, ov.b});
    }
  }

  PSLG graph;

  std::unordered_map<
      Point64,
      int,
      PointHash,
      PointEq>
      vertexMap;

  auto getVertex =
      [&](const Point64& p) {
        auto it = vertexMap.find(p);

        if (it != vertexMap.end())
          return it->second;

        int idx =
            (int)graph.vertices.size();

        graph.vertices.push_back({p});

        vertexMap[p] = idx;

        return idx;
      };

  std::set<std::pair<int, int>> edgeSet;

  for (size_t i = 0; i < n; ++i) {
    auto& pts = splits[i];

    std::sort(
        pts.begin(),
        pts.end(),
        [](auto& a, auto& b) {
          return a.t < b.t;
        });

    pts.erase(
        std::unique(
            pts.begin(),
            pts.end(),
            [](auto& a, auto& b) {
              return a.p == b.p;
            }),
        pts.end());

    for (size_t k = 0;
         k + 1 < pts.size();
         ++k) {
      Point64 a = pts[k].p;
      Point64 b = pts[k + 1].p;

      if (a == b)
        continue;

      int va = getVertex(a);
      int vb = getVertex(b);

      if (va > vb)
        std::swap(va, vb);

      if (edgeSet.insert(
                     {va, vb})
              .second) {
        graph.edges.push_back(
            {va,
             vb});
      }
    }
  }

  return graph;
}

Point64 SamplePoint(
    const PSLG& graph,
    const Face& cycle) {
  // Get edge midpoint
  auto p0 =
      graph.vertices[cycle.vertices[0]].p;

  auto p1 =
      graph.vertices[cycle.vertices[1]].p;

  Point64 mid{
      (p0.x + p1.x) / 2,
      (p0.y + p1.y) / 2};

  auto dx = p1.x - p0.x;
  auto dy = p1.y - p0.y;

  if (cycle.area > 0) {
    // Nudge left
    mid.x -= dy * 0.1;
    mid.y += dx * 0.1;
  } else {
    mid.x += dy * 0.1;
    mid.y -= dx * 0.1;
  }

  return mid;
}

std::vector<Face> ExtractMinimalCycles(
    const PSLG& graph) {
  struct HalfEdge {
    int origin;
    int dest;
    int twin;

    int next = -1;

    bool visited = false;

    double angle;
  };

  std::vector<HalfEdge> halfEdges;
  halfEdges.reserve(graph.edges.size() * 2);

  std::vector<std::vector<int>>
      outgoing(graph.vertices.size());

  //
  // Build half-edges
  //
  for (const auto& e : graph.edges) {
    int h0 = (int)halfEdges.size();

    halfEdges.push_back({e.v0,
                         e.v1,
                         h0 + 1,
                         -1,
                         false,
                         0.0});

    halfEdges.push_back({e.v1,
                         e.v0,
                         h0,
                         -1,
                         false,
                         0.0});

    outgoing[e.v0].push_back(h0);
    outgoing[e.v1].push_back(h0 + 1);
  }

  //
  // Compute angles
  //
  for (auto& h : halfEdges) {
    auto& a =
        graph.vertices[h.origin].p;

    auto& b =
        graph.vertices[h.dest].p;

    h.angle =
        std::atan2(
            double(b.y - a.y),
            double(b.x - a.x));
  }

  //
  // Sort outgoing edges CCW
  //
  for (auto& list : outgoing) {
    std::sort(
        list.begin(),
        list.end(),
        [&](int lhs, int rhs) {
          return halfEdges[lhs].angle <
                 halfEdges[rhs].angle;
        });
  }

  //
  // Build next-face pointers
  //
  for (size_t v = 0;
       v < outgoing.size();
       ++v) {
    auto& list = outgoing[v];

    int n = (int)list.size();

    for (int i = 0; i < n; ++i) {
      int h = list[i];

      int twin =
          halfEdges[h].twin;

      int twinOrigin =
          halfEdges[twin].origin;

      auto& twinList =
          outgoing[twinOrigin];

      auto it =
          std::find(
              twinList.begin(),
              twinList.end(),
              twin);

      int idx =
          (int)std::distance(
              twinList.begin(),
              it);

      //
      // Previous edge in CCW order
      //
      int nextIdx =
          (idx - 1 + (int)twinList.size()) %
          (int)twinList.size();

      halfEdges[h].next =
          twinList[nextIdx];
    }
  }

  std::vector<Face> faces;

  //
  // Walk faces
  //
  for (size_t h = 0;
       h < halfEdges.size();
       ++h) {
    if (halfEdges[h].visited)
      continue;

    Face face;

    int start = (int)h;
    int cur = start;

    do {
      halfEdges[cur].visited = true;

      face.vertices.push_back(
          halfEdges[cur].origin);

      cur = halfEdges[cur].next;

    } while (cur != start);

    //
    // Compute signed area
    //
    double area = 0.0;

    int n =
        (int)face.vertices.size();

    for (int i = 0; i < n; ++i) {
      const auto& p0 =
          graph.vertices[face.vertices[i]].p;

      const auto& p1 =
          graph.vertices[face.vertices[(i + 1) % n]].p;

      area +=
          double(p0.x) * double(p1.y) -
          double(p0.y) * double(p1.x);
    }

    face.area = area * 0.5;

    face.interiorPoint = SamplePoint(graph, face);

    faces.push_back(
        std::move(face));
  }

  return faces;
}

bool PointOnSegment(
    const Point64& p,
    const Point64& a,
    const Point64& b) {
  if (Cross(a, b, p) != 0)
    return false;

  return p.x >= std::min(a.x, b.x) &&
         p.x <= std::max(a.x, b.x) &&
         p.y >= std::min(a.y, b.y) &&
         p.y <= std::max(a.y, b.y);
}

int PointInPolygon(
    const Point64& p,
    const Path64& poly) {
  int winding = 0;

  const int n =
      static_cast<int>(poly.size());

  for (int i = 0; i < n; ++i) {
    const Point64& a = poly[i];
    const Point64& b = poly[(i + 1) % n];

    if (PointOnSegment(p, a, b))
      return -1;

    if (a.y <= p.y) {
      if (b.y > p.y) {
        if (Cross(a, b, p) > 0)
          ++winding;
      }
    } else {
      if (b.y <= p.y) {
        if (Cross(a, b, p) < 0)
          --winding;
      }
    }
  }

  return winding == 0 ? 0 : 1;
}

void ClassifyFaces(
    std::vector<Face>& faces,
    const std::vector<bw::core::Clipper2Polygon>& polygons) {
  for (Face& face : faces) {
    std::unordered_set<uint32_t> primIndices;
    face.filled = false;

    for (const auto& polygon : polygons) {
      if (!PointInPolygon(
              face.interiorPoint,
              polygon.path)) {
        continue;
      }

      primIndices.insert(polygon.primitiveIndex);
      face.filled = true;
    }

    face.primitiveIndices.assign(
        primIndices.begin(),
        primIndices.end());
  }
}

struct PolygonNode {
  int cycleIndex;

  int parent = -1;

  std::vector<int> children;

  int depth = 0;
};

struct Box {
  int64_t minx;
  int64_t miny;
  int64_t maxx;
  int64_t maxy;
};

Box GetBounds(
    const PSLG& graph,
    const Face& cycle) {
  Box b;

  const auto& p0 =
      graph.vertices[cycle.vertices[0]].p;

  b.minx = b.maxx = p0.x;
  b.miny = b.maxy = p0.y;

  for (int v : cycle.vertices) {
    const auto& p =
        graph.vertices[v].p;

    b.minx = std::min(b.minx, p.x);
    b.maxx = std::max(b.maxx, p.x);

    b.miny = std::min(b.miny, p.y);
    b.maxy = std::max(b.maxy, p.y);
  }

  return b;
}

bool ContainsBox(
    const Box& outer,
    const Box& inner) {
  return outer.minx < inner.minx &&
         outer.maxx > inner.maxx &&
         outer.miny < inner.miny &&
         outer.maxy > inner.maxy;
}

bool PointInCycle(
    const PSLG& graph,
    const Face& cycle,
    const Point64& pt) {
  bool inside = false;

  int n =
      (int)cycle.vertices.size();

  for (int i = 0, j = n - 1;
       i < n;
       j = i++) {
    auto a =
        graph.vertices[cycle.vertices[i]].p;

    auto b =
        graph.vertices[cycle.vertices[j]].p;

    bool intersect =
        ((a.y > pt.y) !=
         (b.y > pt.y)) &&
        (pt.x <
         (double)(b.x - a.x) *
                 (pt.y - a.y) /
                 (double)(b.y - a.y) +
             a.x);

    if (intersect)
      inside = !inside;
  }

  return inside;
}

std::vector<PolygonNode>
BuildPolygonHierarchy(
    const PSLG& graph,
    const std::vector<Face>& cycles) {
  int n = (int)cycles.size();

  std::vector<PolygonNode> nodes(n);

  std::vector<Box> boxes(n);

  for (int i = 0; i < n; ++i) {
    nodes[i].cycleIndex = i;

    boxes[i] =
        GetBounds(
            graph,
            cycles[i]);
  }

  //
  // Find immediate parent
  //
  for (int i = 0; i < n; ++i) {
    Point64 sample =
        SamplePoint(
            graph,
            cycles[i]);

    double bestArea =
        std::numeric_limits<double>::max();

    int bestParent = -1;

    for (int j = 0; j < n; ++j) {
      if (i == j)
        continue;

      if (!ContainsBox(
              boxes[j],
              boxes[i])) {
        continue;
      }

      if (!PointInCycle(
              graph,
              cycles[j],
              sample)) {
        continue;
      }

      double area =
          std::abs(
              cycles[j].area);

      if (area < bestArea) {
        bestArea = area;
        bestParent = j;
      }
    }

    nodes[i].parent =
        bestParent;
  }

  //
  // Build child lists
  //
  for (int i = 0; i < n; ++i) {
    int parent =
        nodes[i].parent;

    if (parent >= 0) {
      nodes[parent]
          .children
          .push_back(i);
    }
  }

  //
  // Compute depth
  //
  std::function<void(int, int)> dfs =
      [&](int idx,
          int depth) {
        nodes[idx].depth =
            depth;

        for (int child :
             nodes[idx].children) {
          dfs(
              child,
              depth + 1);
        }
      };

  for (int i = 0; i < n; ++i) {
    if (nodes[i].parent == -1) {
      dfs(i, 0);
    }
  }

  return nodes;
}

std::vector<Face> SetFaceTypeAndFilter(std::vector<Face> const& faces, std::vector<PolygonNode> const& hierarchy) {
  std::vector<Face> res;

  for (size_t i = 0; i < hierarchy.size(); ++i) {
    auto const& node = hierarchy[i];

    if (node.parent < 0 && faces[i].area < 0) {
      // Unbounded
    } else if ((node.parent < 0 && faces[i].filled) || !faces[node.parent].filled) {
      res.push_back(faces[i]);
      res.back().type = FaceType::Regular;
    } else if (faces[i].filled && node.parent >= 0 && faces[node.parent].filled) {
      if (faces[i].area > 0) {
        // If we have an island, then we know that the next face will be the
        // corresponding hole.  The hole and the island need to have their places switched.

        res.push_back(faces[i]);

        auto& f = res.back();

        f.type = FaceType::Hole;
        f.area = -f.area;
        f.filled = false;
        f.winding = -f.winding;
        std::reverse(f.vertices.begin(), f.vertices.end());

        res.push_back(faces[i]);
        res.back().type = FaceType::Island;
        i += 1;
      } else {
        res.push_back(faces[i]);
        res.back().type = FaceType::Hole;
        res.back().filled = false;
      }
    } else if (!faces[i].filled && node.parent >= 0 && faces[node.parent].filled && faces[i].area < 0) {
      res.push_back(faces[i]);
      res.back().type = FaceType::Hole;
    }
  }

  return res;
}

}  // namespace expr

////////////////////////////////////////////////////////////////
// Test cases
struct Expected {
  int vertices;
  int edges;
  int cycles;
  int roots;
};

static int CountRoots(
    const std::vector<expr::PolygonNode>& nodes) {
  int count = 0;

  for (auto& n : nodes) {
    if (n.parent == -1)
      ++count;
  }

  return count;
}

static void RunBasicTest(
    std::vector<bw::core::Clipper2Polygon> const& polygons,
    const Expected& expected) {
  // Create graph
  expr::PSLG pslg = expr::BuildPSLG(polygons);

  // Get cycles
  auto cycles = expr::ExtractMinimalCycles(pslg);

  // Set faces as being either filled or not
  ClassifyFaces(cycles, polygons);

  // Build hierarchy.
  auto hierarchy = expr::BuildPolygonHierarchy(pslg, cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  EXPECT_EQ(
      (int)pslg.vertices.size(),
      expected.vertices);

  EXPECT_EQ(
      (int)pslg.edges.size(),
      expected.edges);

  EXPECT_EQ(
      (int)cycles.size(),
      expected.cycles);

  EXPECT_EQ(
      CountRoots(hierarchy),
      expected.roots);
}

using namespace Clipper2Lib;
using namespace expr;

TEST(PSLG, SingleRectangle) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}}};

  RunBasicTest(
      polygons,
      {4,
       4,
       1,
       1});
}

TEST(PSLG, TwoDisconnectedRectangles) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},
          {false,
           ~0u,
           {{20, 0},
            {30, 0},
            {30, 10},
            {20, 10}}}};

  RunBasicTest(
      polygons,
      {8,
       8,
       2,
       2});
}

TEST(PSLG, PolygonWithHole) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {true,
           ~0u,
           {{5, 5},
            {5, 15},
            {15, 15},
            {15, 5}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      hierarchy.size(),
      2u);
}

TEST(PSLG, PolygonWithIsland) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {false,
           ~0u,
           {{5, 5},
            {15, 5},
            {15, 15},
            {5, 15}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      cycles.size(),
      3u);

  ASSERT_EQ(
      hierarchy.size(),
      3u);
}

TEST(PSLG, PolygonWithIslandAndHoleInIt) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {20, 0},
            {20, 20},
            {0, 20}}},
          {false,
           ~0u,
           {{5, 5},
            {15, 5},
            {15, 15},
            {5, 15}}},
          {true,
           ~0u,
           {{8, 8},
            {8, 12},
            {12, 12},
            {12, 8}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      cycles.size(),
      3u);

  ASSERT_EQ(
      hierarchy.size(),
      3u);
}

TEST(PSLG, TwoPolygonsWithHole) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {100, 0},
            {100, 100},
            {0, 100}}},
          {true,
           ~0u,
           {{10, 10},
            {10, 90},
            {90, 90},
            {90, 10}}},
          {false,
           ~0u,
           {{50, 50},
            {150, 50},
            {150, 150},
            {50, 150}}},
          {true,
           ~0u,
           {{60, 60},
            {60, 140},
            {140, 140},
            {140, 60}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      hierarchy.size(),
      9u);
}

TEST(PSLG, HoleWithIsland) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {30, 0},
            {30, 30},
            {0, 30}}},
          {true,
           ~0u,
           {{5, 5},
            {5, 25},
            {25, 25},
            {25, 5}}},
          {false,
           ~0u,
           {{10, 10},
            {20, 10},
            {20, 20},
            {10, 20}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      hierarchy.size(),
      3u);

  std::vector<int> depths;

  for (auto& n : hierarchy)
    depths.push_back(n.depth);

  std::sort(
      depths.begin(),
      depths.end());

  EXPECT_EQ(depths[0], 0);
  EXPECT_EQ(depths[1], 1);
  EXPECT_EQ(depths[2], 2);
}

TEST(PSLG, TwoHoles) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {40, 0},
            {40, 40},
            {0, 40}}},

          {true,
           ~0u,
           {{5, 5},
            {5, 15},
            {15, 15},
            {15, 5}}},

          {true,
           ~0u,
           {{25, 5},
            {35, 5},
            {35, 15},
            {25, 15}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  EXPECT_EQ(hierarchy.size(), 3);
}

TEST(PSLG, SharedEdge) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},

          {false,
           ~0u,
           {{10, 0},
            {20, 0},
            {20, 10},
            {10, 10}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, TouchingVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {10, 0},
            {10, 10},
            {0, 10}}},

          {false,
           ~0u,
           {{10, 10},
            {20, 10},
            {15, 20}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      2u);
}

TEST(PSLG, FiveLevelHierarchy) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {true, ~0u, {{10, 10}, {10, 90}, {90, 90}, {90, 10}}},
          {false, ~0u, {{20, 20}, {80, 20}, {80, 80}, {20, 80}}},
          {true, ~0u, {{30, 30}, {30, 70}, {70, 70}, {70, 30}}},
          {false, ~0u, {{40, 40}, {60, 40}, {60, 60}, {40, 60}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  ClassifyFaces(cycles, polygons);
  auto hierarchy =
      BuildPolygonHierarchy(
          pslg,
          cycles);

  cycles = SetFaceTypeAndFilter(cycles, hierarchy);

  ASSERT_EQ(
      hierarchy.size(),
      5u);

  std::vector<int> depths;

  for (auto& n : hierarchy)
    depths.push_back(n.depth);

  std::sort(
      depths.begin(),
      depths.end());

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(
        depths[i],
        i);
  }
}

TEST(PSLG, Grid3x3) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {30, 0},
            {30, 30},
            {0, 30}}},

          {false,
           ~0u,
           {{10, 0},
            {20, 0},
            {20, 30},
            {10, 30}}},

          {false,
           ~0u,
           {{0, 10},
            {30, 10},
            {30, 20},
            {0, 20}}}};

  PSLG pslg =
      BuildPSLG(polygons);

  auto cycles =
      ExtractMinimalCycles(pslg);

  EXPECT_EQ(
      cycles.size(),
      9u);
}

TEST(PSLG_Pathological, FourWayVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
          {false, ~0u, {{0, 10}, {10, 10}, {10, 20}, {0, 20}}},
          {false, ~0u, {{10, 10}, {20, 10}, {20, 20}, {10, 20}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, TJunction) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {30, 0}, {30, 20}, {0, 20}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(Hierarchy_Pathological, EightChildren) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {false, ~0u, {{20, 20}, {40, 20}, {40, 40}, {20, 40}}},
          {false, ~0u, {{45, 20}, {55, 20}, {55, 40}, {45, 40}}},
          {false, ~0u, {{60, 20}, {80, 20}, {80, 40}, {60, 40}}},
          {false, ~0u, {{20, 45}, {40, 45}, {40, 55}, {20, 55}}},
          {false, ~0u, {{60, 45}, {80, 45}, {80, 55}, {60, 55}}},
          {false, ~0u, {{20, 60}, {40, 60}, {40, 80}, {20, 80}}},
          {false, ~0u, {{45, 60}, {55, 60}, {55, 80}, {45, 80}}},
          {false, ~0u, {{60, 60}, {80, 60}, {80, 80}, {60, 80}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  ClassifyFaces(cycles, polygons);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  cycles = SetFaceTypeAndFilter(cycles, tree);

  EXPECT_GE(tree.size(), 9u);
}

TEST(Hierarchy_Pathological, DeepNesting) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  for (int i = 0; i < 5; ++i) {
    int s = i * 10;

    Path64 p;

    if (i % 2 == 1) {
      p = {
          {s, s},
          {s, 100 - s},
          {100 - s, 100 - s},
          {100 - s, s}};
    } else {
      p = {
          {s, s},
          {100 - s, s},
          {100 - s, 100 - s},
          {s, 100 - s}};
    }

    polygons.push_back(
        {i % 2 == 1,
         ~0u,
         p});
  }

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);
  ClassifyFaces(cycles, polygons);
  auto tree = BuildPolygonHierarchy(pslg, cycles);
  cycles = SetFaceTypeAndFilter(cycles, tree);

  ASSERT_EQ(tree.size(), 5u);

  int maxDepth = 0;

  for (auto& n : tree)
    maxDepth = std::max(maxDepth, n.depth);

  EXPECT_EQ(maxDepth, 4);
}

TEST(PSLG_Pathological, SharedEdgeChain) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}},
          {false, ~0u, {{10, 0}, {20, 0}, {20, 10}, {10, 10}}},
          {false, ~0u, {{20, 0}, {30, 0}, {30, 10}, {20, 10}}},
          {false, ~0u, {{30, 0}, {40, 0}, {40, 10}, {30, 10}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, Pinwheel) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false, ~0u, {{0, 0}, {50, 50}, {0, 100}}},
          {false, ~0u, {{0, 100}, {50, 50}, {100, 100}}},
          {false, ~0u, {{100, 100}, {50, 50}, {100, 0}}},
          {false, ~0u, {{100, 0}, {50, 50}, {0, 0}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 4u);
}

TEST(PSLG_Pathological, ThinSliver) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0},
            {1000000, 0},
            {1000000, 1},
            {0, 1}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 1u);
}

TEST(PSLG_Pathological, Comb) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  polygons.push_back(
      {false, ~0u, {{0, 0}, {100, 0}, {100, 20}, {0, 20}}});

  for (int i = 0; i < 10; ++i) {
    int x = 5 + i * 9;

    polygons.push_back(
        {false, ~0u, {{x, 20}, {x + 3, 20}, {x + 3, 40}, {x, 40}}});
  }

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_GE(cycles.size(), 11u);
}

TEST(PSLG_Pathological, HoleTouchesShellAtVertex) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0}, {100, 0}, {100, 100}, {0, 100}}},
          {true,
           ~0u,
           {{0, 0}, {20, 0}, {20, 20}, {0, 20}}}};

  EXPECT_NO_THROW(
      {
        auto pslg = BuildPSLG(polygons);
      });
}

TEST(PSLG_Pathological, FigureEightTouch) {
  std::vector<bw::core::Clipper2Polygon> polygons =
      {
          {false,
           ~0u,
           {{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
          {false,
           ~0u,
           {{20, 20}, {40, 20}, {40, 40}, {20, 40}}}};

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 2u);
}

TEST(PSLG_Pathological, Grid20x20) {
  std::vector<bw::core::Clipper2Polygon> polygons;

  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 20; ++x) {
      polygons.push_back(
          {false,
           ~0u,
           {{x * 10, y * 10},
            {(x + 1) * 10, y * 10},
            {(x + 1) * 10, (y + 1) * 10},
            {x * 10, (y + 1) * 10}}});
    }
  }

  auto pslg = BuildPSLG(polygons);
  auto cycles = ExtractMinimalCycles(pslg);

  EXPECT_EQ(cycles.size(), 400u);
}

TEST(PSLG_Fuzz, RandomRectangles) {
  std::mt19937 rng(12345);

  for (int iter = 0; iter < 1000; ++iter) {
    std::vector<bw::core::Clipper2Polygon> polygons;

    for (int i = 0; i < 50; ++i) {
      int x = rng() % 1000;
      int y = rng() % 1000;

      int w = 10 + rng() % 100;
      int h = 10 + rng() % 100;

      polygons.push_back(
          {false,
           ~0u,
           {{x, y},
            {x + w, y},
            {x + w, y + h},
            {x, y + h}}});
    }

    EXPECT_NO_THROW(
        {
          auto pslg = BuildPSLG(polygons);
          auto cycles = ExtractMinimalCycles(pslg);
          ClassifyFaces(cycles, polygons);
          auto tree = BuildPolygonHierarchy(pslg, cycles);
          cycles = SetFaceTypeAndFilter(cycles, tree);
        });
  }
}

////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();

  /*
  string filename;
  if (argc < 2)
  {
          filename = "../../../../experiments/resources/test-1.yaml";
  }
  else
  {
          filename = argv[1];
  }

  try
  {
          auto world = openWorld(filename);

          Clipper2Lib::WmInitialiseAllocators(4, 16 * 1024 * 1024);

          // Create intermediate polygons

          // Intersect polygons
          // - Check clipper logic for collinear edges
          // - Use clipper intersection function
          // - Just do n^2 test for now, to prove it works conceptually, before doing sweep-lines
          // - Just split vectors and insert in the middle for now

          // Build graph
          // - How to handle polygons entirely within other polygons?
          //   This should produce "sector within sector", with outer sector having a hole
          // Find minimal cycles
          // - Will need to reconstruct the hole information by testing points

          Clipper2Lib::Paths64 input;

          input.push_back({
                  {0,0},
                  {100,0},
                  {100,100},
                  {0,100}
          });

          // polygon B
          input.push_back({
                  {50,50},
                  {150,50},
                  {150,150},
                  {50,150}
          });

          // Hole in A
          input.push_back({
                  {10,10},
                  {10,20},
                  {20,20},
                  {20,10}
          });

          // Clockwise has negative area.  Therefore, if the input cycle is clockwise (hole),
          // we keep the negative one.
          // So: if it's a hole, then it will be clockwise, and we only keep the negative loop
          // If it's not a hole, we want to keep both, because if the polygon is inside another,
          // we will need it both as a hole for the containing polygon, and a polygon in its own right.
          // If it's not inside another, then we remove the clockwise (negative area) loop.
          // Clipper will return correctly-ordered edges to us
          auto graph = expr::BuildPSLG(input);
          auto cycles = expr::ExtractMinimalCycles(graph);
          auto hierarchy = expr::BuildPolygonHierarchy(graph, cycles);

          Clipper2Lib::WmDestroyAllocators();
  }
  catch (std::exception& e)
  {
          cout << e.what() << "\n";
          return 1;
  }

  return 0;
  */
}