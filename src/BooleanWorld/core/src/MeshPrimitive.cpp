#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include <willpower/geometry/Edge.h>
#include <willpower/geometry/MeshOperations.h>
#include <willpower/geometry/Polygon.h>
#include <willpower/geometry/Vertex.h>

#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/MeshPrimitive.h"

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr double GeometryEpsilon = 1e-6;
constexpr size_t MaxTreeRings = 1024;
constexpr size_t MaxTreeVertices = 1048576;
constexpr uint32_t TreeFormatMagic = 0x4d545245;  // "MTRE"

struct PointLocation {
  bool inside{};
  bool boundary{};
};

double cross(wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& c) {
  return (double(b.x) - a.x) * (double(c.y) - a.y) -
         (double(b.y) - a.y) * (double(c.x) - a.x);
}

bool nearZero(double value) {
  return abs(value) <= GeometryEpsilon;
}

bool pointOnSegment(wp::Vector2 const& p, wp::Vector2 const& a, wp::Vector2 const& b) {
  return nearZero(cross(a, b, p)) &&
         p.x >= min(a.x, b.x) - GeometryEpsilon &&
         p.x <= max(a.x, b.x) + GeometryEpsilon &&
         p.y >= min(a.y, b.y) - GeometryEpsilon &&
         p.y <= max(a.y, b.y) + GeometryEpsilon;
}

PointLocation locatePoint(ClosedPolygon const& ring, wp::Vector2 const& point) {
  bool inside = false;
  for (size_t i = 0, previous = ring.size() - 1; i < ring.size(); previous = i++) {
    auto const& a = ring[previous].p;
    auto const& b = ring[i].p;
    if (pointOnSegment(point, a, b)) {
      return {false, true};
    }
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return {inside, false};
}

double twiceArea(ClosedPolygon const& ring) {
  double result = 0.0;
  for (size_t i = 0; i < ring.size(); ++i) {
    auto const& a = ring[i].p;
    auto const& b = ring[(i + 1) % ring.size()].p;
    result += double(a.x) * b.y - double(b.x) * a.y;
  }
  return result;
}

int orientation(wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& c) {
  auto value = cross(a, b, c);
  return value > GeometryEpsilon ? 1 : value < -GeometryEpsilon ? -1
                                                                : 0;
}

bool segmentsIntersect(
    wp::Vector2 const& a, wp::Vector2 const& b,
    wp::Vector2 const& c, wp::Vector2 const& d) {
  auto abC = orientation(a, b, c);
  auto abD = orientation(a, b, d);
  auto cdA = orientation(c, d, a);
  auto cdB = orientation(c, d, b);
  return (abC != abD && cdA != cdB) ||
         (abC == 0 && pointOnSegment(c, a, b)) ||
         (abD == 0 && pointOnSegment(d, a, b)) ||
         (cdA == 0 && pointOnSegment(a, c, d)) ||
         (cdB == 0 && pointOnSegment(b, c, d));
}

bool properSegmentsIntersect(
    wp::Vector2 const& a, wp::Vector2 const& b,
    wp::Vector2 const& c, wp::Vector2 const& d) {
  return orientation(a, b, c) * orientation(a, b, d) < 0 &&
         orientation(c, d, a) * orientation(c, d, b) < 0;
}

bool ringsCoincide(ClosedPolygon const& first, ClosedPolygon const& second) {
  if (first.size() != second.size() || first.empty()) {
    return false;
  }
  for (size_t start = 0; start < second.size(); ++start) {
    if (first.front().p != second[start].p) {
      continue;
    }
    bool forward = true;
    bool reverse = true;
    for (size_t i = 0; i < first.size(); ++i) {
      forward &= first[i].p == second[(start + i) % second.size()].p;
      reverse &= first[i].p == second[(start + second.size() - i) % second.size()].p;
    }
    if (forward || reverse) {
      return true;
    }
  }
  return false;
}

void validateRing(ClosedPolygon& ring, size_t& ringCount, size_t& vertexCount) {
  if (++ringCount > MaxTreeRings ||
      (vertexCount += ring.size()) > MaxTreeVertices) {
    throw CoreException("MeshPrimitive containment tree exceeds its aggregate resource limit.");
  }
  if (ring.size() < 3 || ring.size() > BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
    throw CoreException("A MeshPrimitive Ring must contain a valid number of vertices.");
  }
  for (size_t i = 0; i < ring.size(); ++i) {
    auto const& p = ring[i].p;
    auto const& next = ring[(i + 1) % ring.size()].p;
    if (!isfinite(p.x) || !isfinite(p.y) || p == next) {
      throw CoreException("A MeshPrimitive Ring contains a malformed vertex or edge.");
    }
  }
  for (size_t i = 0; i < ring.size(); ++i) {
    for (size_t j = i + 1; j < ring.size(); ++j) {
      if (j == i + 1 || (i == 0 && j + 1 == ring.size())) {
        continue;
      }
      if (segmentsIntersect(
              ring[i].p, ring[(i + 1) % ring.size()].p,
              ring[j].p, ring[(j + 1) % ring.size()].p)) {
        throw CoreException("A MeshPrimitive Ring must be simple.");
      }
    }
  }
  auto area = twiceArea(ring);
  if (nearZero(area)) {
    throw CoreException("A MeshPrimitive Ring must have non-zero area.");
  }
  if (area < 0.0) {
    // Vertex metadata belongs to its outgoing geometric edge. Reversing a
    // Ring reverses every outgoing direction, so each new outgoing edge is
    // the old incoming edge at that vertex. Preserve the metadata on that
    // same segment rather than merely reversing it with its endpoint.
    auto original = ring;
    reverse(ring.begin(), ring.end());
    for (size_t i = 0; i < ring.size(); ++i) {
      auto source = (ring.size() + ring.size() - 2 - i) % ring.size();
      ring[i].edgeFlags = original[source].edgeFlags;
      ring[i].edgeNormalMap = original[source].edgeNormalMap;
      ring[i].edgeWallMask = original[source].edgeWallMask;
    }
  }
}

bool ringContainedBy(ClosedPolygon const& child, ClosedPolygon const& parent) {
  if (ringsCoincide(child, parent)) {
    return true;
  }
  bool hasInteriorPoint = false;
  for (auto const& vertex : child) {
    auto location = locatePoint(parent, vertex.p);
    if (!location.inside && !location.boundary) {
      return false;
    }
    hasInteriorPoint |= location.inside;
  }
  for (size_t i = 0; i < child.size(); ++i) {
    for (size_t j = 0; j < parent.size(); ++j) {
      if (properSegmentsIntersect(
              child[i].p, child[(i + 1) % child.size()].p,
              parent[j].p, parent[(j + 1) % parent.size()].p)) {
        return false;
      }
    }
  }
  return hasInteriorPoint;
}

bool interiorsOverlap(ClosedPolygon const& first, ClosedPolygon const& second) {
  if (ringsCoincide(first, second)) {
    return true;
  }
  for (size_t i = 0; i < first.size(); ++i) {
    for (size_t j = 0; j < second.size(); ++j) {
      if (properSegmentsIntersect(
              first[i].p, first[(i + 1) % first.size()].p,
              second[j].p, second[(j + 1) % second.size()].p)) {
        return true;
      }
    }
  }
  return any_of(first.begin(), first.end(), [&](Vertex const& vertex) {
           return locatePoint(second, vertex.p).inside;
         }) ||
         any_of(second.begin(), second.end(), [&](Vertex const& vertex) {
           return locatePoint(first, vertex.p).inside;
         });
}

template <class Node, class RingGetter>
void validateSiblings(vector<Node> const& nodes, RingGetter ring) {
  for (size_t i = 0; i < nodes.size(); ++i) {
    for (size_t j = i + 1; j < nodes.size(); ++j) {
      if (interiorsOverlap(ring(nodes[i]), ring(nodes[j]))) {
        throw CoreException("MeshPrimitive sibling Ring interiors overlap.");
      }
    }
  }
}

void normalizeAndValidateTree(vector<MeshFilledRegion>& shells) {
  size_t ringCount = 0;
  size_t vertexCount = 0;
  struct FilledWork {
    MeshFilledRegion* node;
    ClosedPolygon const* parent;
    bool ringValidated;
  };
  vector<FilledWork> work;
  for (auto& shell : shells) {
    work.push_back({&shell, nullptr, false});
  }
  while (!work.empty()) {
    auto [filled, parent, ringValidated] = work.back();
    work.pop_back();
    if (!ringValidated) {
      validateRing(filled->ring, ringCount, vertexCount);
    }
    if (parent && !ringContainedBy(filled->ring, *parent)) {
      throw CoreException("A MeshPrimitive Island is not contained by its Hole.");
    }
    for (auto& hole : filled->holes) {
      validateRing(hole.ring, ringCount, vertexCount);
      if (!ringContainedBy(hole.ring, filled->ring)) {
        throw CoreException("A MeshPrimitive Hole is not contained by its filled region.");
      }
      for (auto& island : hole.islands) {
        // Validate before sibling geometry queries; the work item below then
        // validates the Island's own children.
        validateRing(island.ring, ringCount, vertexCount);
      }
      validateSiblings(hole.islands, [](MeshFilledRegion const& island) -> ClosedPolygon const& {
        return island.ring;
      });
      for (auto& island : hole.islands) {
        work.push_back({&island, &hole.ring, true});
      }
    }
    validateSiblings(filled->holes, [](MeshHole const& hole) -> ClosedPolygon const& {
      return hole.ring;
    });
  }
  validateSiblings(shells, [](MeshFilledRegion const& shell) -> ClosedPolygon const& {
    return shell.ring;
  });
}

vector<MeshFilledRegion> shallowTree(vector<ComplexPolygon> const& polygons) {
  vector<MeshFilledRegion> shells;
  shells.reserve(polygons.size());
  for (auto const& polygon : polygons) {
    if (polygon.empty()) {
      throw CoreException("A MeshPrimitive ComplexPolygon requires a Shell Ring.");
    }
    MeshFilledRegion shell{polygon.front(), {}};
    for (size_t ring = 1; ring < polygon.size(); ++ring) {
      shell.holes.push_back({polygon[ring], {}});
    }
    shells.push_back(move(shell));
  }
  normalizeAndValidateTree(shells);
  return shells;
}

void forEachRing(vector<MeshFilledRegion>& shells, auto&& callback) {
  vector<MeshFilledRegion*> filled;
  for (auto& shell : shells) filled.push_back(&shell);
  while (!filled.empty()) {
    auto* node = filled.back();
    filled.pop_back();
    callback(node->ring);
    for (auto& hole : node->holes) {
      callback(hole.ring);
      for (auto& island : hole.islands) filled.push_back(&island);
    }
  }
}

vector<ComplexPolygon> flatten(vector<MeshFilledRegion> const& shells) {
  vector<ComplexPolygon> result;
  vector<MeshFilledRegion const*> work;
  for (auto shell = shells.rbegin(); shell != shells.rend(); ++shell) {
    work.push_back(&*shell);
  }
  while (!work.empty()) {
    auto const* filled = work.back();
    work.pop_back();
    ComplexPolygon polygon;
    polygon.push_back(filled->ring);
    for (auto const& hole : filled->holes) polygon.push_back(hole.ring);
    result.push_back(move(polygon));
    for (auto hole = filled->holes.rbegin(); hole != filled->holes.rend(); ++hole) {
      for (auto island = hole->islands.rbegin(); island != hole->islands.rend(); ++island) {
        work.push_back(&*island);
      }
    }
  }
  return result;
}

void normalizeWorldTree(vector<MeshFilledRegion>& shells, wp::Vector2& centre, float& scale) {
  normalizeAndValidateTree(shells);
  bool first = true;
  wp::Vector2 minimum{}, maximum{};
  forEachRing(shells, [&](ClosedPolygon& ring) {
    for (auto const& vertex : ring) {
      if (first) {
        minimum = maximum = vertex.p;
        first = false;
      } else {
        minimum.x = min(minimum.x, vertex.p.x);
        minimum.y = min(minimum.y, vertex.p.y);
        maximum.x = max(maximum.x, vertex.p.x);
        maximum.y = max(maximum.y, vertex.p.y);
      }
    }
  });
  if (first) {
    centre = {};
    scale = 50.0f;
    return;
  }
  centre = (minimum + maximum) * 0.5f;
  auto half = (maximum - minimum) * 0.5f;
  scale = max(half.x, half.y);
  if (!(scale > 0.0f) || !isfinite(scale)) {
    throw CoreException("A MeshPrimitive tree cannot be normalized.");
  }
  forEachRing(shells, [&](ClosedPolygon& ring) {
    for (auto& vertex : ring) vertex.p = (vertex.p - centre) / scale;
  });
}

}  // namespace

struct MeshPrimitiveEditingProxy::Impl {
  struct Filled;
  struct Hole {
    uint32_t polygonIndex{};
    uint32_t anchorVertexIndex{};
    vector<Filled> islands;
  };
  struct Filled {
    uint32_t polygonIndex{};
    uint32_t anchorVertexIndex{};
    vector<Hole> holes;
  };

  wp::geometry::Mesh mesh;
  vector<Filled> shells;

  // Per-edge flags, keyed by Mesh edge index. Populated whenever a Mesh edge
  // is created (see Builder::addRing) by copying in the source Vertex's
  // edgeFlags. Entries for edges that no longer exist are simply stale and
  // are never queried again.
  unordered_map<uint32_t, uint32_t> edgeFlags;
  unordered_map<uint32_t, WallNormalMapOverride> edgeNormalMaps;
  unordered_map<uint32_t, WallMaskOverride> edgeWallMasks;

  uint32_t rawEdgeFlags(uint32_t edgeIndex) const {
    auto found = edgeFlags.find(edgeIndex);
    return found != edgeFlags.end() ? found->second : uint32_t{0};
  }

  bool isEdgeExternal(uint32_t edgeIndex) const {
    if (mesh.edgeIndexIterationFinished(edgeIndex)) return false;
    return mesh.getEdge(edgeIndex).getConnectivity() == wp::geometry::Edge::External;
  }

  optional<bool> edgeCollisionOverride(uint32_t edgeIndex) const {
    if (!isEdgeExternal(edgeIndex)) return nullopt;
    auto flags = rawEdgeFlags(edgeIndex);
    if ((flags & BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG) == 0) return nullopt;
    return (flags & BW_MESH_EDGE_COLLIDES_FLAG) != 0;
  }

  bool effectiveEdgeVisible(uint32_t edgeIndex) const {
    if (!isEdgeExternal(edgeIndex)) return false;
    return (rawEdgeFlags(edgeIndex) & BW_MESH_EDGE_INVISIBLE_FLAG) == 0;
  }

  WallNormalMapOverride edgeNormalMap(uint32_t edgeIndex) const {
    if (!isEdgeExternal(edgeIndex)) return WallNormalMapOverride::unset();
    auto found = edgeNormalMaps.find(edgeIndex);
    return found == edgeNormalMaps.end() ? WallNormalMapOverride::unset()
                                         : found->second;
  }

  WallMaskOverride edgeWallMask(uint32_t edgeIndex) const {
    if (!isEdgeExternal(edgeIndex)) return WallMaskOverride::unset();
    auto found = edgeWallMasks.find(edgeIndex);
    return found == edgeWallMasks.end() ? WallMaskOverride::unset()
                                        : found->second;
  }

  struct ExactPointLess {
    bool operator()(wp::Vector2 const& left, wp::Vector2 const& right) const {
      auto bits = [](float value) {
        return value == 0.0f ? uint32_t{} : bit_cast<uint32_t>(value);
      };
      return pair{bits(left.x), bits(left.y)} < pair{bits(right.x), bits(right.y)};
    }
  };

  struct Builder {
    Impl& target;
    map<wp::Vector2, uint32_t, ExactPointLess> vertices;
    map<pair<uint32_t, uint32_t>, uint32_t> edges;

    uint32_t addRing(ClosedPolygon const& ring, uint32_t& anchorVertexIndex) {
      wp::geometry::IndexVector vertexIndices;
      wp::geometry::IndexVector edgeData;
      for (auto const& vertex : ring) {
        auto [found, inserted] = vertices.try_emplace(vertex.p, 0);
        if (inserted) {
          found->second = target.mesh.addVertex(wp::geometry::Vertex(vertex.p));
        }
        vertexIndices.push_back(found->second);
      }
      anchorVertexIndex = vertexIndices.front();
      for (size_t i = 0; i < vertexIndices.size(); ++i) {
        auto first = vertexIndices[i];
        auto second = vertexIndices[(i + 1) % vertexIndices.size()];
        auto key = minmax(first, second);
        auto [found, inserted] = edges.try_emplace(key, 0);
        if (inserted) {
          found->second = target.mesh.addEdge(wp::geometry::Edge(first, second));
          // ring[i]'s outgoing edge is (first, second): the edge to the
          // next vertex in this Ring. Copy its stored flags in.
          target.edgeFlags[found->second] = ring[i].edgeFlags;
          target.edgeNormalMaps[found->second] = ring[i].edgeNormalMap;
          target.edgeWallMasks[found->second] = ring[i].edgeWallMask;
        }
        edgeData.insert(edgeData.end(), {first, second, found->second});
      }
      return target.mesh.addPolygon(wp::geometry::Polygon(edgeData));
    }

    Filled addFilled(MeshFilledRegion const& source) {
      Filled result;
      result.polygonIndex = addRing(source.ring, result.anchorVertexIndex);
      for (auto const& sourceHole : source.holes) {
        Hole hole;
        hole.polygonIndex = addRing(sourceHole.ring, hole.anchorVertexIndex);
        target.mesh.addHoleToPolygon(result.polygonIndex, hole.polygonIndex);
        for (auto const& island : sourceHole.islands) {
          hole.islands.push_back(addFilled(island));
        }
        result.holes.push_back(move(hole));
      }
      return result;
    }
  };

  void rebuild(vector<MeshFilledRegion> const& worldTree) {
    mesh.clear();
    shells.clear();
    edgeFlags.clear();
    edgeNormalMaps.clear();
    edgeWallMasks.clear();
    Builder builder{*this};
    for (auto const& shell : worldTree) shells.push_back(builder.addFilled(shell));
  }

  ClosedPolygon readMappedRing(
      wp::geometry::Mesh const& source,
      uint32_t polygonIndex,
      uint32_t anchorVertexIndex) const {
    auto ordered = source.getPolygon(polygonIndex).getOrderedVertexIndices();
    auto anchor = find(ordered.begin(), ordered.end(), anchorVertexIndex);
    if (anchor != ordered.end()) rotate(ordered.begin(), anchor, ordered.end());
    ClosedPolygon result;
    for (auto vertex : ordered) result.emplace_back(source.getVertex(vertex).getPosition());
    if (twiceArea(result) < 0.0 && result.size() > 1) {
      reverse(next(result.begin()), result.end());
      reverse(next(ordered.begin()), ordered.end());
    }
    // Write each vertex's outgoing-edge flags back from the Mesh-edge-keyed
    // map, using the (possibly reversed) vertex-index ordering so structural
    // edits, which operate on these Ring vertices, inherit the correct
    // per-edge state.
    for (size_t i = 0; i < ordered.size(); ++i) {
      auto next = (i + 1) % ordered.size();
      auto edgeIndex = source.getEdgeIndexByVertices(ordered[i], ordered[next]);
      if (edgeIndex >= 0) {
        auto index = static_cast<uint32_t>(edgeIndex);
        result[i].edgeFlags = rawEdgeFlags(index);
        auto normalMap = edgeNormalMaps.find(index);
        if (normalMap != edgeNormalMaps.end()) {
          result[i].edgeNormalMap = normalMap->second;
        }
        auto wallMask = edgeWallMasks.find(index);
        if (wallMask != edgeWallMasks.end()) {
          result[i].edgeWallMask = wallMask->second;
        }
      }
    }
    return result;
  }

  vector<MeshFilledRegion> readTree(wp::geometry::Mesh const& sourceMesh) const {
    auto readFilled = [&](auto&& self, Filled const& source) -> MeshFilledRegion {
      MeshFilledRegion result{
          readMappedRing(sourceMesh, source.polygonIndex, source.anchorVertexIndex), {}};
      for (auto const& sourceHole : source.holes) {
        MeshHole hole{
            readMappedRing(sourceMesh, sourceHole.polygonIndex, sourceHole.anchorVertexIndex), {}};
        for (auto const& island : sourceHole.islands) {
          hole.islands.push_back(self(self, island));
        }
        result.holes.push_back(move(hole));
      }
      return result;
    };
    vector<MeshFilledRegion> result;
    for (auto const& shell : shells) result.push_back(readFilled(readFilled, shell));
    return result;
  }

  vector<MeshFilledRegion> readTree() const { return readTree(mesh); }

  bool mappingTopologyMatches(wp::geometry::Mesh const& candidate) const {
    set<uint32_t> live;
    for (auto index = candidate.getFirstPolygonIndex();
         !candidate.polygonIndexIterationFinished(index);
         index = candidate.getNextPolygonIndex(index)) {
      live.insert(index);
    }
    bool valid = true;
    auto visit = [&](auto&& self, Filled const& filled) -> void {
      auto check = [&](uint32_t polygonIndex) {
        valid &= live.contains(polygonIndex);
        if (valid) {
          // replaceMesh is a geometry-only entry point. In particular, it may
          // not duplicate the vertices or edges of just one side of a welded
          // boundary: no editor operation implicitly unwelds authored Rings.
          valid &= mesh.getPolygon(polygonIndex).getVertexIndexSet() ==
                       candidate.getPolygon(polygonIndex).getVertexIndexSet() &&
                   mesh.getPolygon(polygonIndex).getEdgeIndexSet() ==
                       candidate.getPolygon(polygonIndex).getEdgeIndexSet();
        }
      };
      check(filled.polygonIndex);
      for (auto const& hole : filled.holes) {
        check(hole.polygonIndex);
        for (auto const& island : hole.islands) self(self, island);
      }
    };
    for (auto const& shell : shells) visit(visit, shell);
    return valid;
  }
};

MeshPrimitive::MeshPrimitive()
    : Primitive(Operation::Union, FillRule::EvenOdd) {
}

MeshPrimitive::MeshPrimitive(Operation operation, vector<MeshFilledRegion> shells, LocalTreeTag)
    : Primitive(operation, FillRule::EvenOdd), mShells(move(shells)) {
  replaceTree(move(mShells));
}

MeshPrimitive::MeshPrimitive(MeshPrimitive const& other) {
  copyFrom(other);
}

MeshPrimitive& MeshPrimitive::operator=(MeshPrimitive const& other) {
  if (this != &other) copyFrom(other);
  return *this;
}

MeshPrimitive* MeshPrimitive::fromTree(Operation operation, vector<MeshFilledRegion> shells) {
  wp::Vector2 centre;
  float scale;
  normalizeWorldTree(shells, centre, scale);
  auto* primitive = new MeshPrimitive(operation, move(shells), LocalTreeTag{});
  primitive->setSize(scale * 2.0f, scale * 2.0f);
  primitive->setPosition(centre);
  primitive->updateVertexPositions();
  return primitive;
}

MeshPrimitive* MeshPrimitive::fromComplexPolygons(
    Operation operation, vector<ComplexPolygon> polygons) {
  auto shells = shallowTree(polygons);  // validates before any object exists
  wp::Vector2 centre;
  float scale;
  normalizeWorldTree(shells, centre, scale);
  auto* primitive = new MeshPrimitive(operation, move(shells), LocalTreeTag{});
  primitive->setSize(scale * 2.0f, scale * 2.0f);
  primitive->setPosition(centre);
  {
    auto mutation = primitive->mutate();
    mutation.animation(VertexTransformer::Key::Scale).setDefaultStructure({{0.0f, 1.0f}, {1.0f, 1.0f}}, {{Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::Angle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitAngle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitDistance).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{Easing::Linear}}, true);
  }
  primitive->updateVertexPositions();
  return primitive;
}

vector<MeshFilledRegion> const& MeshPrimitive::getShells() const {
  return mShells;
}

vector<ComplexPolygon> MeshPrimitive::flattenTree() const {
  return flatten(mShells);
}

vector<MeshPrimitive*> MeshPrimitive::decomposeFilledRegions() const {
  vector<MeshFilledRegion const*> filledRegions;
  auto collect = [&](auto&& self, MeshFilledRegion const& filled) -> void {
    filledRegions.push_back(&filled);
    for (auto const& hole : filled.holes) {
      for (auto const& island : hole.islands) self(self, island);
    }
  };
  for (auto const& shell : mShells) collect(collect, shell);

  if (filledRegions.size() < 2) return {};

  vector<MeshPrimitive*> result;
  result.reserve(filledRegions.size());
  for (auto const* filled : filledRegions) {
    auto region = *filled;
    for (auto& hole : region.holes) hole.islands.clear();

    auto* part = static_cast<MeshPrimitive*>(copy());
    part->replaceTree({move(region)});
    part->setOperation(Operation::Union);
    part->setPriority(getPriority());
    result.push_back(part);
  }
  return result;
}

void MeshPrimitive::replaceTree(vector<MeshFilledRegion> shells) {
  normalizeAndValidateTree(shells);
  auto polygons = flatten(shells);
  mShells = move(shells);
  setVertices(polygons);
}

vector<ComplexPolygon> MeshPrimitive::generateVerticesImpl() {
  return flattenTree();
}

void MeshPrimitive::polygonsUpdated() {
  notifyWorldPolygonsChanged();
}

void MeshPrimitive::rotateAuthoredGeometry(float angle, wp::Vector2 const& origin) {
  auto candidate = mShells;
  forEachRing(candidate, [&](ClosedPolygon& ring) {
    for (auto& vertex : ring) {
      vertex.p -= origin;
      vertex.p.rotateClockwise(angle);
      vertex.p += origin;
    }
  });
  replaceTree(move(candidate));
}

unique_ptr<MeshPrimitiveEditingProxy> MeshPrimitive::createEditingProxy() const {
  return unique_ptr<MeshPrimitiveEditingProxy>(new MeshPrimitiveEditingProxy(*this));
}

MeshPrimitiveEditingProxy::MeshPrimitiveEditingProxy(MeshPrimitive const& primitive)
    : mImpl(make_unique<Impl>()) {
  MeshPrimitive restPose(primitive);
  restPose.calculateAnimationValues();
  auto worldTree = restPose.mShells;
  forEachRing(worldTree, [&](ClosedPolygon& ring) {
    for (auto& vertex : ring) {
      vertex.p = restPose.transformVertex(vertex.p * restPose.getSize(), nullptr);
    }
  });
  mImpl->rebuild(worldTree);
}

MeshPrimitiveEditingProxy::~MeshPrimitiveEditingProxy() = default;
MeshPrimitiveEditingProxy::MeshPrimitiveEditingProxy(MeshPrimitiveEditingProxy&&) noexcept = default;
MeshPrimitiveEditingProxy& MeshPrimitiveEditingProxy::operator=(
    MeshPrimitiveEditingProxy&& other) noexcept {
  if (this != &other) *mImpl = move(*other.mImpl);
  return *this;
}
MeshPrimitiveEditingProxy::MeshPrimitiveEditingProxy(MeshPrimitiveEditingProxy const& other)
    : mImpl(make_unique<Impl>(*other.mImpl)) {}
MeshPrimitiveEditingProxy& MeshPrimitiveEditingProxy::operator=(
    MeshPrimitiveEditingProxy const& other) {
  if (this != &other) *mImpl = *other.mImpl;
  return *this;
}

wp::geometry::Mesh const& MeshPrimitiveEditingProxy::getMesh() const {
  return mImpl->mesh;
}

vector<MeshPrimitiveEditingProxy::NodeMapping> MeshPrimitiveEditingProxy::getNodeMappings() const {
  vector<NodeMapping> result;
  auto visit = [&](auto&& self, Impl::Filled const& filled, NodeRole role,
                   uint32_t parent) -> void {
    result.push_back({filled.polygonIndex, role, parent});
    for (auto const& hole : filled.holes) {
      result.push_back({hole.polygonIndex, NodeRole::Hole, filled.polygonIndex});
      for (auto const& island : hole.islands) {
        self(self, island, NodeRole::Island, hole.polygonIndex);
      }
    }
  };
  for (auto const& shell : mImpl->shells) visit(visit, shell, NodeRole::Shell, ~0u);
  return result;
}

bool MeshPrimitiveEditingProxy::replaceMesh(wp::geometry::Mesh mesh) {
  if (!mImpl->mappingTopologyMatches(mesh)) return false;
  try {
    auto candidateTree = mImpl->readTree(mesh);
    normalizeAndValidateTree(candidateTree);
  } catch (exception const&) {
    return false;
  }
  mImpl->mesh = move(mesh);
  return true;
}

void MeshPrimitiveEditingProxy::moveVertex(uint32_t vertexIndex, wp::Vector2 const& delta) {
  mImpl->mesh.moveVertex(vertexIndex, delta);
}

void MeshPrimitiveEditingProxy::moveVertices(
    wp::geometry::IndexVector const& vertexIndices, wp::Vector2 const& delta) {
  mImpl->mesh.moveVertices(vertexIndices, delta);
}

void MeshPrimitiveEditingProxy::moveEdge(uint32_t edgeIndex, wp::Vector2 const& delta) {
  mImpl->mesh.moveEdge(edgeIndex, delta);
}

void MeshPrimitiveEditingProxy::moveRing(uint32_t polygonIndex, wp::Vector2 const& delta) {
  mImpl->mesh.movePolygon(polygonIndex, delta);
}

bool MeshPrimitiveEditingProxy::splitEdge(
    uint32_t edgeIndex, float t,
    wp::geometry::SplitEdgeResult* result) {
  auto originalFlags = mImpl->rawEdgeFlags(edgeIndex);
  auto originalNormalMap = mImpl->edgeNormalMap(edgeIndex);
  auto originalWallMask = mImpl->edgeWallMask(edgeIndex);
  wp::geometry::SplitEdgeResult localResult;
  auto* target = result ? result : &localResult;
  wp::geometry::MeshOperations::splitEdge(&mImpl->mesh, edgeIndex, t, target);
  // Both halves of a split edge inherit the original edge's flags. The
  // underlying operation reuses the original edge index for one half
  // (newEdgeIndices[0], already correct) and allocates a brand-new index for
  // the other (newEdgeIndices[1]).
  if (target->newEdgeIndices.size() == 2) {
    mImpl->edgeFlags[target->newEdgeIndices[0]] = originalFlags;
    mImpl->edgeFlags[target->newEdgeIndices[1]] = originalFlags;
    mImpl->edgeNormalMaps[target->newEdgeIndices[0]] = originalNormalMap;
    mImpl->edgeNormalMaps[target->newEdgeIndices[1]] = originalNormalMap;
    mImpl->edgeWallMasks[target->newEdgeIndices[0]] = originalWallMask;
    mImpl->edgeWallMasks[target->newEdgeIndices[1]] = originalWallMask;
  }
  return !target->newEdgeIndices.empty();
}

bool MeshPrimitiveEditingProxy::splitEdge(
    uint32_t edgeIndex, wp::geometry::SplitEdgeResult* result) {
  return splitEdge(edgeIndex, 0.5f, result);
}

optional<bool> MeshPrimitiveEditingProxy::getEdgeCollisionOverride(
    uint32_t edgeIndex) const {
  return mImpl->edgeCollisionOverride(edgeIndex);
}

bool MeshPrimitiveEditingProxy::setEdgeCollisionOverride(
    uint32_t edgeIndex, optional<bool> collides) {
  if (!isEdgeCollisionEditable(edgeIndex)) return false;
  auto flags = mImpl->rawEdgeFlags(edgeIndex);
  if (!collides.has_value()) {
    flags &= ~static_cast<uint32_t>(
        BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG | BW_MESH_EDGE_COLLIDES_FLAG);
  } else {
    flags |= BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG;
    if (*collides) {
      flags |= BW_MESH_EDGE_COLLIDES_FLAG;
    } else {
      flags &= ~static_cast<uint32_t>(BW_MESH_EDGE_COLLIDES_FLAG);
    }
  }
  mImpl->edgeFlags[edgeIndex] = flags;
  return true;
}

bool MeshPrimitiveEditingProxy::isEdgeCollisionEditable(uint32_t edgeIndex) const {
  return mImpl->isEdgeExternal(edgeIndex);
}

bool MeshPrimitiveEditingProxy::getEdgeVisible(uint32_t edgeIndex) const {
  return mImpl->effectiveEdgeVisible(edgeIndex);
}

bool MeshPrimitiveEditingProxy::isEdgeVisibilityEditable(uint32_t edgeIndex) const {
  return mImpl->isEdgeExternal(edgeIndex);
}

bool MeshPrimitiveEditingProxy::setEdgeVisible(uint32_t edgeIndex, bool visible) {
  if (!isEdgeVisibilityEditable(edgeIndex)) return false;
  auto flags = mImpl->rawEdgeFlags(edgeIndex);
  if (visible) {
    flags &= ~static_cast<uint32_t>(BW_MESH_EDGE_INVISIBLE_FLAG);
  } else {
    flags |= BW_MESH_EDGE_INVISIBLE_FLAG;
  }
  mImpl->edgeFlags[edgeIndex] = flags;
  return true;
}

WallNormalMapOverride MeshPrimitiveEditingProxy::getEdgeNormalMapOverride(
    uint32_t edgeIndex) const {
  return mImpl->edgeNormalMap(edgeIndex);
}

bool MeshPrimitiveEditingProxy::isEdgeNormalMapEditable(
    uint32_t edgeIndex) const {
  return mImpl->isEdgeExternal(edgeIndex);
}

bool MeshPrimitiveEditingProxy::setEdgeNormalMapOverride(
    uint32_t edgeIndex, WallNormalMapOverride const& overrideValue) {
  if (!isEdgeNormalMapEditable(edgeIndex)) return false;
  mImpl->edgeNormalMaps[edgeIndex] = overrideValue;
  return true;
}

WallMaskOverride MeshPrimitiveEditingProxy::getEdgeWallMaskOverride(
    uint32_t edgeIndex) const {
  return mImpl->edgeWallMask(edgeIndex);
}

bool MeshPrimitiveEditingProxy::isEdgeWallMaskEditable(
    uint32_t edgeIndex) const {
  return mImpl->isEdgeExternal(edgeIndex);
}

bool MeshPrimitiveEditingProxy::setEdgeWallMaskOverride(
    uint32_t edgeIndex, WallMaskOverride const& overrideValue) {
  if (!isEdgeWallMaskEditable(edgeIndex)) return false;
  mImpl->edgeWallMasks[edgeIndex] = overrideValue;
  return true;
}

bool MeshPrimitiveEditingProxy::sliceFilledRing(
    uint32_t polygonIndex, uint32_t firstVertexIndex,
    uint32_t secondVertexIndex) {
  if (firstVertexIndex == secondVertexIndex ||
      mImpl->mesh.vertexIndexIterationFinished(firstVertexIndex) ||
      mImpl->mesh.vertexIndexIterationFinished(secondVertexIndex) ||
      mImpl->mesh.polygonIndexIterationFinished(polygonIndex)) {
    return false;
  }

  auto mappings = getNodeMappings();
  auto mapping = find_if(mappings.begin(), mappings.end(), [&](auto const& item) {
    return item.polygonIndex == polygonIndex && item.role != NodeRole::Hole;
  });
  if (mapping == mappings.end()) return false;

  auto const& polygon = mImpl->mesh.getPolygon(polygonIndex);
  auto const& polygonVertices = polygon.getVertexIndexSet();
  if (!polygonVertices.contains(firstVertexIndex) ||
      !polygonVertices.contains(secondVertexIndex)) {
    return false;
  }
  auto ordered = polygon.getOrderedVertexIndices();
  auto first = find(ordered.begin(), ordered.end(), firstVertexIndex);
  auto second = find(ordered.begin(), ordered.end(), secondVertexIndex);
  if (first == ordered.end() || second == ordered.end()) return false;
  auto firstOffset = static_cast<size_t>(first - ordered.begin());
  auto secondOffset = static_cast<size_t>(second - ordered.begin());
  auto distance = firstOffset > secondOffset ? firstOffset - secondOffset
                                             : secondOffset - firstOffset;
  if (distance == 1 || distance + 1 == ordered.size()) return false;

  auto const& firstPosition = mImpl->mesh.getVertex(firstVertexIndex).getPosition();
  auto const& secondPosition = mImpl->mesh.getVertex(secondVertexIndex).getPosition();
  ClosedPolygon targetRing;
  for (auto vertexIndex : ordered) {
    targetRing.emplace_back(mImpl->mesh.getVertex(vertexIndex).getPosition());
  }
  if (!locatePoint(targetRing, (firstPosition + secondPosition) / 2.0f).inside) {
    return false;
  }

  // Every contact with existing topology is refused except contact at the
  // chord's own endpoints. An Edge incident to an endpoint meets the chord
  // there by construction - that is what makes the Vertex an endpoint - and
  // whatever else is welded at it is entitled to be: this Ring's Hole and
  // Island family, a sibling Ring an earlier Slice made of it, another
  // filled region joined at that Vertex. What such an Edge may not do is run
  // *along* the chord, which is the one way it can meet it at more than the
  // shared Vertex.
  //
  // A coincident Vertex belonging to a foreign Ring is a different Vertex,
  // not a shared one, so its Edges are never exempt and still have to clear
  // the crossing test below.
  auto isEndpoint = [&](uint32_t vertexIndex) {
    return vertexIndex == firstVertexIndex || vertexIndex == secondVertexIndex;
  };
  for (auto edgeIndex = mImpl->mesh.getFirstEdgeIndex();
       !mImpl->mesh.edgeIndexIterationFinished(edgeIndex);
       edgeIndex = mImpl->mesh.getNextEdgeIndex(edgeIndex)) {
    auto const& edge = mImpl->mesh.getEdge(edgeIndex);
    auto const& edgeFirst =
        mImpl->mesh.getVertex(edge.getFirstVertex()).getPosition();
    auto const& edgeSecond =
        mImpl->mesh.getVertex(edge.getSecondVertex()).getPosition();

    if (isEndpoint(edge.getFirstVertex()) || isEndpoint(edge.getSecondVertex())) {
      // The chord already exists as an Edge, or an incident Edge lies along
      // it: either way the chord is not a new division of the Ring.
      if (isEndpoint(edge.getFirstVertex()) &&
          isEndpoint(edge.getSecondVertex())) {
        return false;
      }
      auto const& other = isEndpoint(edge.getFirstVertex()) ? edgeSecond
                                                            : edgeFirst;
      if (pointOnSegment(other, firstPosition, secondPosition)) {
        return false;
      }
      continue;
    }

    if (segmentsIntersect(
            firstPosition, secondPosition, edgeFirst, edgeSecond)) {
      return false;
    }
  }

  auto makePath = [&](size_t from, size_t to) {
    ClosedPolygon ring;
    for (auto index = from;; index = (index + 1) % ordered.size()) {
      Vertex vertex(mImpl->mesh.getVertex(ordered[index]).getPosition());
      if (index == to) {
        // Closing this path creates an Internal Slice chord. Leave collision
        // unset; if a later edit exposes it, generation determines collision
        // until the user authors an override.
        vertex.edgeFlags &= ~static_cast<uint32_t>(
            BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG |
            BW_MESH_EDGE_COLLIDES_FLAG);
      } else {
        auto next = (index + 1) % ordered.size();
        auto edgeIndex =
            mImpl->mesh.getEdgeIndexByVertices(ordered[index], ordered[next]);
        if (edgeIndex >= 0) {
          auto index = static_cast<uint32_t>(edgeIndex);
          vertex.edgeFlags = mImpl->rawEdgeFlags(index);
          vertex.edgeNormalMap = mImpl->edgeNormalMap(index);
          vertex.edgeWallMask = mImpl->edgeWallMask(index);
        }
      }
      ring.push_back(vertex);
      if (index == to) break;
    }
    return ring;
  };
  MeshFilledRegion firstPart{makePath(firstOffset, secondOffset), {}};
  MeshFilledRegion secondPart{makePath(secondOffset, firstOffset), {}};

  auto candidate = mImpl->readTree();
  vector<MeshFilledRegion>* siblings = nullptr;
  size_t siblingIndex = 0;
  auto locateFilled = [&](auto&& self, vector<MeshFilledRegion>& nodes,
                          vector<Impl::Filled> const& nodeMappings) -> bool {
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (nodeMappings[i].polygonIndex == mapping->polygonIndex) {
        siblings = &nodes;
        siblingIndex = i;
        return true;
      }
      for (size_t hole = 0; hole < nodes[i].holes.size(); ++hole) {
        if (self(self, nodes[i].holes[hole].islands,
                 nodeMappings[i].holes[hole].islands)) {
          return true;
        }
      }
    }
    return false;
  };
  if (!locateFilled(locateFilled, candidate, mImpl->shells)) return false;

  auto source = move((*siblings)[siblingIndex]);
  for (auto& hole : source.holes) {
    if (ringContainedBy(hole.ring, firstPart.ring)) {
      firstPart.holes.push_back(move(hole));
    } else if (ringContainedBy(hole.ring, secondPart.ring)) {
      secondPart.holes.push_back(move(hole));
    } else {
      return false;
    }
  }

  (*siblings)[siblingIndex] = move(firstPart);
  siblings->insert(siblings->begin() + siblingIndex + 1, move(secondPart));
  try {
    normalizeAndValidateTree(candidate);
  } catch (exception const&) {
    return false;
  }
  mImpl->rebuild(candidate);
  return true;
}

bool MeshPrimitiveEditingProxy::mutateRings(
    function<bool(ClosedPolygon&)> mutation) {
  auto candidate = mImpl->readTree();
  bool changed = false;
  forEachRing(candidate, [&](ClosedPolygon& ring) { changed |= mutation(ring); });
  if (!changed) return false;
  try {
    auto validated = candidate;
    normalizeAndValidateTree(validated);
    mImpl->rebuild(candidate);
    return true;
  } catch (exception const&) {
    return false;
  }
}

bool MeshPrimitiveEditingProxy::removeVertex(uint32_t vertexIndex) {
  auto const position = mImpl->mesh.getVertex(vertexIndex).getPosition();

  // Removing a vertex coalesces its incoming and outgoing edges. Unlike the
  // older boolean edge flags, Image normal-map values are not safely
  // lossy-mergeable: accepting two distinct values would silently choose one
  // wall surface. Refuse the whole candidate before touching any welded Ring.
  for (auto polygon = mImpl->mesh.getFirstPolygonIndex();
       !mImpl->mesh.polygonIndexIterationFinished(polygon);
       polygon = mImpl->mesh.getNextPolygonIndex(polygon)) {
    auto ordered = mImpl->mesh.getPolygon(polygon).getOrderedVertexIndices();
    auto found = find_if(ordered.begin(), ordered.end(), [&](uint32_t index) {
      return mImpl->mesh.getVertex(index).getPosition() == position;
    });
    if (found == ordered.end()) continue;
    auto offset = static_cast<size_t>(found - ordered.begin());
    auto previous = ordered[(offset + ordered.size() - 1) % ordered.size()];
    auto next = ordered[(offset + 1) % ordered.size()];
    auto incoming = mImpl->mesh.getEdgeIndexByVertices(previous, *found);
    auto outgoing = mImpl->mesh.getEdgeIndexByVertices(*found, next);
    if (incoming >= 0 && outgoing >= 0 &&
        (mImpl->edgeNormalMap(static_cast<uint32_t>(incoming)) !=
             mImpl->edgeNormalMap(static_cast<uint32_t>(outgoing)) ||
         mImpl->edgeWallMask(static_cast<uint32_t>(incoming)) !=
             mImpl->edgeWallMask(static_cast<uint32_t>(outgoing)))) {
      return false;
    }
  }

  return mutateRings([&](ClosedPolygon& ring) {
    auto oldSize = ring.size();
    erase_if(ring, [&](Vertex const& vertex) { return vertex.p == position; });
    return ring.size() != oldSize;
  });
}

bool MeshPrimitiveEditingProxy::removeEdge(uint32_t edgeIndex) {
  if (mImpl->mesh.edgeIndexIterationFinished(edgeIndex)) return false;
  auto const& edge = mImpl->mesh.getEdge(edgeIndex);
  auto first = mImpl->mesh.getVertex(edge.getFirstVertex()).getPosition();
  auto second = mImpl->mesh.getVertex(edge.getSecondVertex()).getPosition();

  if (edge.getPolygonReferences().size() == 2) {
    auto references = vector<uint32_t>(
        edge.getPolygonReferences().begin(), edge.getPolygonReferences().end());
    auto mappings = getNodeMappings();
    auto firstMapping = find_if(mappings.begin(), mappings.end(), [&](auto const& mapping) {
      return mapping.polygonIndex == references[0];
    });
    auto secondMapping = find_if(mappings.begin(), mappings.end(), [&](auto const& mapping) {
      return mapping.polygonIndex == references[1];
    });
    if (firstMapping == mappings.end() || secondMapping == mappings.end() ||
        firstMapping->role != secondMapping->role ||
        firstMapping->parentPolygonIndex != secondMapping->parentPolygonIndex) {
      return false;
    }

    auto candidate = mImpl->readTree();
    struct FilledLocation {
      vector<MeshFilledRegion>* siblings{};
      size_t index{};
    };
    struct HoleLocation {
      vector<MeshHole>* siblings{};
      size_t index{};
    };
    array<FilledLocation, 2> filledLocations{};
    array<HoleLocation, 2> holeLocations{};
    auto locate = [&](auto&& self, vector<MeshFilledRegion>& filled,
                      vector<Impl::Filled> const& filledMappings) -> void {
      for (size_t i = 0; i < filled.size(); ++i) {
        for (size_t target = 0; target < references.size(); ++target) {
          if (filledMappings[i].polygonIndex == references[target]) {
            filledLocations[target] = {&filled, i};
          }
        }
        for (size_t h = 0; h < filled[i].holes.size(); ++h) {
          for (size_t target = 0; target < references.size(); ++target) {
            if (filledMappings[i].holes[h].polygonIndex == references[target]) {
              holeLocations[target] = {&filled[i].holes, h};
            }
          }
          self(self, filled[i].holes[h].islands,
               filledMappings[i].holes[h].islands);
        }
      }
    };
    locate(locate, candidate, mImpl->shells);

    auto mergedBoundary = [&](ClosedPolygon const& firstRing,
                              ClosedPolygon const& secondRing) {
      auto alternatePath = [&](ClosedPolygon const& ring,
                               wp::Vector2 const& from,
                               wp::Vector2 const& to) {
        ClosedPolygon path;
        auto start = find_if(ring.begin(), ring.end(), [&](Vertex const& vertex) {
          return vertex.p == from;
        });
        auto finish = find_if(ring.begin(), ring.end(), [&](Vertex const& vertex) {
          return vertex.p == to;
        });
        if (start == ring.end() || finish == ring.end()) return path;
        auto startIndex = static_cast<size_t>(start - ring.begin());
        auto finishIndex = static_cast<size_t>(finish - ring.begin());
        bool forwardIsShared = (startIndex + 1) % ring.size() == finishIndex;
        auto index = startIndex;
        for (;;) {
          path.push_back(ring[index]);
          if (index == finishIndex) break;
          index = forwardIsShared
                      ? (index + ring.size() - 1) % ring.size()
                      : (index + 1) % ring.size();
        }
        return path;
      };

      auto firstPath = alternatePath(firstRing, first, second);
      auto secondPath = alternatePath(secondRing, second, first);
      if (firstPath.size() < 2 || secondPath.size() < 2) {
        return ClosedPolygon{};
      }
      ClosedPolygon result = move(firstPath);
      result.insert(
          result.end(), next(secondPath.begin()), prev(secondPath.end()));
      return result;
    };

    if (firstMapping->role == NodeRole::Hole) {
      if (!holeLocations[0].siblings ||
          holeLocations[0].siblings != holeLocations[1].siblings) {
        return false;
      }
      auto& siblings = *holeLocations[0].siblings;
      auto low = min(holeLocations[0].index, holeLocations[1].index);
      auto high = max(holeLocations[0].index, holeLocations[1].index);
      auto boundary = mergedBoundary(siblings[low].ring, siblings[high].ring);
      if (boundary.empty()) return false;
      auto merged = move(siblings[low]);
      merged.ring = move(boundary);
      merged.islands.insert(
          merged.islands.end(),
          make_move_iterator(siblings[high].islands.begin()),
          make_move_iterator(siblings[high].islands.end()));
      siblings[low] = move(merged);
      siblings.erase(siblings.begin() + high);
    } else {
      if (!filledLocations[0].siblings ||
          filledLocations[0].siblings != filledLocations[1].siblings) {
        return false;
      }
      auto& siblings = *filledLocations[0].siblings;
      auto low = min(filledLocations[0].index, filledLocations[1].index);
      auto high = max(filledLocations[0].index, filledLocations[1].index);
      auto boundary = mergedBoundary(siblings[low].ring, siblings[high].ring);
      if (boundary.empty()) return false;
      auto merged = move(siblings[low]);
      merged.ring = move(boundary);
      merged.holes.insert(
          merged.holes.end(),
          make_move_iterator(siblings[high].holes.begin()),
          make_move_iterator(siblings[high].holes.end()));
      siblings[low] = move(merged);
      siblings.erase(siblings.begin() + high);
    }

    try {
      normalizeAndValidateTree(candidate);
    } catch (exception const&) {
      return false;
    }
    mImpl->rebuild(candidate);
    return true;
  }

  auto midpoint = (first + second) / 2.0f;
  return mutateRings([&](ClosedPolygon& ring) {
    bool changed = false;
    for (auto& vertex : ring) {
      if (vertex.p == second) {
        vertex.p = midpoint;
        changed = true;
      }
    }
    auto oldSize = ring.size();
    erase_if(ring, [&](Vertex const& vertex) { return vertex.p == first; });
    return changed || ring.size() != oldSize;
  });
}

bool MeshPrimitiveEditingProxy::removeRing(uint32_t polygonIndex) {
  auto candidate = mImpl->readTree();
  bool removed = false;
  auto pruneFilled = [&](auto&& self, vector<MeshFilledRegion>& filled,
                         vector<Impl::Filled> const& mappings) -> void {
    for (size_t i = filled.size(); i-- > 0;) {
      if (mappings[i].polygonIndex == polygonIndex) {
        filled.erase(filled.begin() + i);
        removed = true;
        continue;
      }
      for (size_t h = filled[i].holes.size(); h-- > 0;) {
        if (mappings[i].holes[h].polygonIndex == polygonIndex) {
          filled[i].holes.erase(filled[i].holes.begin() + h);
          removed = true;
        } else {
          self(self, filled[i].holes[h].islands, mappings[i].holes[h].islands);
        }
      }
    }
  };
  pruneFilled(pruneFilled, candidate, mImpl->shells);
  if (removed) mImpl->rebuild(candidate);
  return removed;
}

uint32_t MeshPrimitiveEditingProxy::addShell(ClosedPolygon ring) {
  auto candidate = mImpl->readTree();
  candidate.push_back({move(ring), {}});
  normalizeAndValidateTree(candidate);
  mImpl->rebuild(candidate);
  return mImpl->shells.back().polygonIndex;
}

uint32_t MeshPrimitiveEditingProxy::addHole(
    uint32_t filledPolygonIndex, ClosedPolygon ring) {
  auto candidate = mImpl->readTree();
  uint32_t result = ~0u;
  auto add = [&](auto&& self, vector<MeshFilledRegion>& filled,
                 vector<Impl::Filled> const& mappings) -> void {
    for (size_t i = 0; i < filled.size(); ++i) {
      if (mappings[i].polygonIndex == filledPolygonIndex) {
        filled[i].holes.push_back({ring, {}});
        result = uint32_t(i);  // found marker
        return;
      }
      for (size_t h = 0; h < filled[i].holes.size(); ++h)
        self(self, filled[i].holes[h].islands, mappings[i].holes[h].islands);
    }
  };
  add(add, candidate, mImpl->shells);
  if (result == ~0u) return ~0u;
  normalizeAndValidateTree(candidate);
  mImpl->rebuild(candidate);
  for (auto const& mapping : getNodeMappings())
    if (mapping.role == NodeRole::Hole && mapping.parentPolygonIndex == filledPolygonIndex)
      result = mapping.polygonIndex;
  return result;
}

uint32_t MeshPrimitiveEditingProxy::addIsland(
    uint32_t holePolygonIndex, ClosedPolygon ring) {
  auto candidate = mImpl->readTree();
  bool found = false;
  auto add = [&](auto&& self, vector<MeshFilledRegion>& filled,
                 vector<Impl::Filled> const& mappings) -> void {
    for (size_t i = 0; i < filled.size(); ++i) {
      for (size_t h = 0; h < filled[i].holes.size(); ++h) {
        if (mappings[i].holes[h].polygonIndex == holePolygonIndex) {
          filled[i].holes[h].islands.push_back({ring, {}});
          found = true;
          return;
        }
        self(self, filled[i].holes[h].islands, mappings[i].holes[h].islands);
      }
    }
  };
  add(add, candidate, mImpl->shells);
  if (!found) return ~0u;
  normalizeAndValidateTree(candidate);
  mImpl->rebuild(candidate);
  for (auto const& mapping : getNodeMappings())
    if (mapping.role == NodeRole::Island && mapping.parentPolygonIndex == holePolygonIndex)
      return mapping.polygonIndex;
  return ~0u;
}

uint32_t MeshPrimitiveEditingProxy::fillHole(uint32_t holePolygonIndex) {
  auto candidate = mImpl->readTree();
  bool found = false;
  auto fill = [&](auto&& self, vector<MeshFilledRegion>& filled,
                  vector<Impl::Filled> const& mappings) -> void {
    for (size_t i = 0; i < filled.size(); ++i) {
      for (size_t h = 0; h < filled[i].holes.size(); ++h) {
        auto& hole = filled[i].holes[h];
        if (mappings[i].holes[h].polygonIndex == holePolygonIndex) {
          MeshFilledRegion wrapper{hole.ring, {}};
          for (auto& island : hole.islands) {
            wrapper.holes.push_back({island.ring, {move(island)}});
          }
          hole.islands = {move(wrapper)};
          found = true;
          return;
        }
        self(self, hole.islands, mappings[i].holes[h].islands);
      }
    }
  };
  fill(fill, candidate, mImpl->shells);
  if (!found) return ~0u;
  normalizeAndValidateTree(candidate);

  // Rebuild in structural pre-order on a detached Impl. That is the same
  // deterministic ordering used after Undo and save/reload, while Builder's
  // exact coordinate maps reconstruct every coincident boundary as welded
  // proxy topology. Publishing only the completed Impl keeps Fill Hole atomic.
  Impl updated;
  updated.rebuild(candidate);
  Impl::Hole* target = nullptr;
  auto findHole = [&](auto&& self, vector<Impl::Filled>& filled) -> void {
    for (auto& region : filled) {
      for (auto& hole : region.holes) {
        if (hole.polygonIndex == holePolygonIndex) {
          target = &hole;
          return;
        }
        self(self, hole.islands);
        if (target) return;
      }
    }
  };
  findHole(findHole, updated.shells);
  if (!target || target->islands.empty()) return ~0u;
  auto result = target->islands.front().polygonIndex;
  *mImpl = move(updated);
  return result;
}

void MeshPrimitiveEditingProxy::commitTo(MeshPrimitive& primitive) const {
  auto candidate = mImpl->readTree();
  MeshPrimitive restPose(primitive);
  restPose.calculateAnimationValues();
  auto origin = restPose.transformVertex({0.0f, 0.0f}, nullptr);
  auto xAxis = restPose.transformVertex({1.0f, 0.0f}, nullptr) - origin;
  auto yAxis = restPose.transformVertex({0.0f, 1.0f}, nullptr) - origin;
  auto determinant = xAxis.x * yAxis.y - xAxis.y * yAxis.x;
  if (abs(determinant) <= numeric_limits<float>::epsilon()) {
    throw CoreException("Cannot commit a MeshPrimitive proxy through a singular rest-pose transform.");
  }
  auto toLocal = [&](wp::Vector2 const& world) {
    auto p = world - origin;
    return wp::Vector2{
               (p.x * yAxis.y - p.y * yAxis.x) / determinant,
               (xAxis.x * p.y - xAxis.y * p.x) / determinant} /
           primitive.getSize();
  };
  forEachRing(candidate, [&](ClosedPolygon& ring) {
    for (auto& vertex : ring) vertex.p = toLocal(vertex.p);
  });
  normalizeAndValidateTree(candidate);
  primitive.replaceTree(move(candidate));
}

void MeshPrimitive::setFillRule(FillRule fillRule) {
  if (fillRule != FillRule::EvenOdd) {
    throw CoreException("MeshPrimitive FillRule is fixed to EvenOdd by its containment tree.");
  }
}

Primitive::FillRule MeshPrimitive::getFillRule() const {
  return FillRule::EvenOdd;
}

void MeshPrimitive::copyFrom(MeshPrimitive const& other) {
  Primitive::copyFrom(other);
  mShells = other.mShells;
  // Never trust or promote the inherited compatibility cache to authority.
  mPolygons = flattenTree();
}

Primitive* MeshPrimitive::copy() const {
  return new MeshPrimitive(*this);
}

string MeshPrimitive::getType() const { return "Mesh"; }

string MeshPrimitive::getName() const {
  return getFlags() & BW_PRIMITIVE_GHOST_FLAG ? "Ghost" : "Mesh";
}

void MeshPrimitive::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializePrimitive(serializer, workData, false);

  auto writeRing = [&](ClosedPolygon const& ring) {
    serializer->beginArray("vertices");
    for (auto const& vertex : ring) {
      serializer->beginMap("vertex");
      serializer->writeVector2("p", vertex.p);
      serializer->writeUint32("flags", vertex.edgeFlags);
      serializer->writeUint8(
          "normalMapState", static_cast<uint8_t>(vertex.edgeNormalMap.state()));
      if (auto image = vertex.edgeNormalMap.imageData()) {
        serializer->writeString("normalMapResource", image->resourceName);
        serializer->writeFloat("normalMapRepeat", image->repeat);
        serializer->writeFloat("normalMapStrength", image->strength);
      }
      serializer->writeUint8(
          "wallMaskState", static_cast<uint8_t>(vertex.edgeWallMask.state()));
      if (auto mask = vertex.edgeWallMask.imageData()) {
        serializer->writeString("wallMaskResource", mask->resourceName);
        serializer->writeUint8("wallMaskChannel", mask->channel);
        serializer->beginArray("wallMaskBlendParameters", false);
        for (float parameter : mask->blendParameters) {
          serializer->writeFloat("", parameter);
        }
        serializer->endArray();
      }
      serializer->endMap();
    }
    serializer->endArray();
  };

  enum struct EventType { BeginFilled,
                          EndFilled,
                          BeginHole,
                          EndHole };
  struct Event {
    EventType type;
    MeshFilledRegion const* filled{};
    MeshHole const* hole{};
  };

  serializer->beginMap("meshPrimitive");
  serializer->writeUint32("treeFormat", TreeFormatMagic);
  serializer->writeUint32("edgeOverrideFormat", 5);
  serializer->beginArray("shells");
  vector<Event> events;
  for (auto shell = mShells.rbegin(); shell != mShells.rend(); ++shell) {
    events.push_back({EventType::BeginFilled, &*shell});
  }
  while (!events.empty()) {
    auto event = events.back();
    events.pop_back();
    switch (event.type) {
      case EventType::BeginFilled:
        serializer->beginMap("filledRegion");
        writeRing(event.filled->ring);
        serializer->beginArray("holes");
        events.push_back({EventType::EndFilled});
        for (auto hole = event.filled->holes.rbegin();
             hole != event.filled->holes.rend(); ++hole) {
          events.push_back({EventType::BeginHole, nullptr, &*hole});
        }
        break;
      case EventType::EndFilled:
        serializer->endArray();
        serializer->endMap();
        break;
      case EventType::BeginHole:
        serializer->beginMap("hole");
        writeRing(event.hole->ring);
        serializer->beginArray("islands");
        events.push_back({EventType::EndHole});
        for (auto island = event.hole->islands.rbegin();
             island != event.hole->islands.rend(); ++island) {
          events.push_back({EventType::BeginFilled, &*island});
        }
        break;
      case EventType::EndHole:
        serializer->endArray();
        serializer->endMap();
        break;
    }
  }
  serializer->endArray();
  serializer->endMap();
}

bool MeshPrimitive::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  // All inherited and tree state is read into a detached object. No failed
  // read, including one in common Primitive state, can partially mutate this.
  MeshPrimitive candidate;
  if (!candidate.deserializePrimitive(serializer, workData, false)) {
    copyErrorsAndWarnings(&candidate, true, true);
    return false;
  }
  if (candidate.Primitive::getFillRule() != FillRule::EvenOdd) {
    addDeserializationError(
        "MeshPrimitive FillRule must be EvenOdd for containment-tree input.");
    return false;
  }

  size_t ringCount = 0;
  size_t vertexCount = 0;
  try {
    serializer->beginMap("meshPrimitive");
    uint32_t treeFormat;
    try {
      treeFormat = serializer->readUint32("treeFormat");
    } catch (exception const&) {
      throw CoreException(
          "Legacy flat MeshPrimitive input is unsupported; a containment tree is required.");
    }
    if (treeFormat != TreeFormatMagic) {
      throw CoreException(
          "Legacy or unsupported MeshPrimitive input has no recognized containment tree.");
    }
    uint32_t edgeOverrideFormat;
    if (serializer->isPositional()) {
      edgeOverrideFormat = serializer->readUint32("edgeOverrideFormat");
    } else if (serializer->hasField("edgeOverrideFormat")) {
      edgeOverrideFormat = serializer->readUint32("edgeOverrideFormat");
    } else {
      edgeOverrideFormat =
          serializer->readUint32("collisionOverrideFormat", true, 0);
    }
    if (edgeOverrideFormat < 4 || edgeOverrideFormat > 5) {
      throw CoreException("Unsupported MeshPrimitive edge override format version.");
    }

    auto readRing = [&]() {
      if (++ringCount > MaxTreeRings) {
        throw CoreException(
            "MeshPrimitive containment tree exceeds its aggregate Ring limit.");
      }
      ClosedPolygon ring;
      serializer->beginArray("vertices");
      while (serializer->nextArrayItem()) {
        if (++vertexCount > MaxTreeVertices ||
            ring.size() >= BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
          throw CoreException(
              "MeshPrimitive containment tree exceeds its aggregate vertex limit.");
        }
        serializer->beginMap("vertex");
        ring.emplace_back(serializer->readVector2("p"));
        auto flags = serializer->readUint32(
            "flags", true, BW_MESH_EDGE_COLLIDES_FLAG);
        if (edgeOverrideFormat == 0) {
          // Legacy true was also the untouched default, so it becomes unset.
          // Legacy false was necessarily authored and remains explicit.
          if ((flags & BW_MESH_EDGE_COLLIDES_FLAG) != 0) {
            flags &= ~static_cast<uint32_t>(
                BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG |
                BW_MESH_EDGE_COLLIDES_FLAG);
          } else {
            flags |= BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG;
          }
        }
        ring.back().edgeFlags = flags;
        if (edgeOverrideFormat >= 2) {
          auto state = serializer->readUint8("normalMapState");
          switch (static_cast<WallNormalMapOverride::State>(state)) {
            case WallNormalMapOverride::State::Unset:
              ring.back().edgeNormalMap = WallNormalMapOverride::unset();
              break;
            case WallNormalMapOverride::State::Disabled:
              ring.back().edgeNormalMap = WallNormalMapOverride::disabled();
              break;
            case WallNormalMapOverride::State::Image: {
              // Positional serializers require explicit sequencing; C++ does
              // not define function-argument evaluation order.
              auto resourceName =
                  serializer->readString("normalMapResource");
              auto repeat = serializer->readFloat("normalMapRepeat");
              auto strength = serializer->readFloat("normalMapStrength");
              ring.back().edgeNormalMap = WallNormalMapOverride::image(
                  std::move(resourceName), repeat, strength);
              break;
            }
            default:
              throw CoreException("Unsupported wall normal-map override state.");
          }
        }
        if (edgeOverrideFormat >= 5) {
          auto maskState = serializer->readUint8("wallMaskState");
          switch (static_cast<WallMaskOverride::State>(maskState)) {
            case WallMaskOverride::State::Unset:
              ring.back().edgeWallMask = WallMaskOverride::unset();
              break;
            case WallMaskOverride::State::Disabled:
              ring.back().edgeWallMask = WallMaskOverride::disabled();
              break;
            case WallMaskOverride::State::Image: {
              // Positional serializers require explicit sequencing; C++ does
              // not define function-argument evaluation order.
              auto resourceName =
                  serializer->readString("wallMaskResource");
              auto channel = serializer->readUint8("wallMaskChannel");
              WallMaskOverride::BlendParameters blendParameters{};
              serializer->beginArray("wallMaskBlendParameters");
              size_t index = 0;
              while (serializer->nextArrayItem()) {
                if (index >= blendParameters.size()) {
                  throw CoreException(
                      "Wall mask blend parameters exceed the fixed array width.");
                }
                blendParameters[index++] = serializer->readFloat();
              }
              serializer->endArray();
              if (index != blendParameters.size()) {
                throw CoreException(
                    "Wall mask blend parameters must hold exactly eight values.");
              }
              ring.back().edgeWallMask = WallMaskOverride::image(
                  std::move(resourceName), channel, blendParameters);
              break;
            }
            default:
              throw CoreException("Unsupported wall mask override state.");
          }
        }
        serializer->endMap();
      }
      serializer->endArray();
      return ring;
    };

    enum struct FrameType { Filled,
                            Hole };
    struct Frame {
      FrameType type;
      MeshFilledRegion* filled{};
      MeshHole* hole{};
    };
    vector<Frame> frames;

    auto beginFilled = [&](MeshFilledRegion& filled) {
      serializer->beginMap("filledRegion");
      filled.ring = readRing();
      serializer->beginArray("holes");
      frames.push_back({FrameType::Filled, &filled});
    };

    serializer->beginArray("shells");
    while (true) {
      if (frames.empty()) {
        if (!serializer->nextArrayItem()) {
          serializer->endArray();
          break;
        }
        candidate.mShells.emplace_back();
        beginFilled(candidate.mShells.back());
        continue;
      }

      auto& frame = frames.back();
      if (frame.type == FrameType::Filled) {
        if (serializer->nextArrayItem()) {
          frame.filled->holes.emplace_back();
          auto& hole = frame.filled->holes.back();
          serializer->beginMap("hole");
          hole.ring = readRing();
          serializer->beginArray("islands");
          frames.push_back({FrameType::Hole, nullptr, &hole});
        } else {
          serializer->endArray();
          serializer->endMap();
          frames.pop_back();
        }
      } else if (serializer->nextArrayItem()) {
        frame.hole->islands.emplace_back();
        beginFilled(frame.hole->islands.back());
      } else {
        serializer->endArray();
        serializer->endMap();
        frames.pop_back();
      }
    }
    serializer->endMap();

    normalizeAndValidateTree(candidate.mShells);
    candidate.mPolygons = flatten(candidate.mShells);
    candidate.updateVertexPositions();
  } catch (exception const& error) {
    addDeserializationError(error.what());
    return false;
  }

  copyFrom(candidate);
  return true;
}

float MeshPrimitive::getRadius() const { return 1.0f; }

wp::BoundingBox MeshPrimitive::calculateBounds() const {
  return calculateExactBounds();
}

}  // namespace core
}  // namespace bw
