#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

#include <willpower/geometry/Edge.h>
#include <willpower/geometry/MeshOperations.h>
#include <willpower/geometry/Polygon.h>
#include <willpower/geometry/Vertex.h>

#include "core/CoreException.h"
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
    reverse(ring.begin(), ring.end());
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

MeshPrimitive::MeshPrimitive(
    Operation operation, FillRule fillType, vector<ComplexPolygon> const& polygons)
    : Primitive(operation, fillType), mShells(shallowTree(polygons)) {
  // Keep the compatibility FillRule, while tree-native construction uses
  // EvenOdd. Existing consumers still edit this setting pending contraction.
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
    Operation operation, FillRule fillType, vector<ComplexPolygon> polygons) {
  auto shells = shallowTree(polygons);  // validates before any object exists
  wp::Vector2 centre;
  float scale;
  normalizeWorldTree(shells, centre, scale);
  auto* primitive = new MeshPrimitive(operation, move(shells), LocalTreeTag{});
  primitive->Primitive::setFillRule(fillType);
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
    uint32_t edgeIndex, wp::geometry::SplitEdgeResult* result) {
  wp::geometry::MeshOperations::splitEdge(&mImpl->mesh, edgeIndex, 0.5f, result);
  return !result || !result->newEdgeIndices.empty();
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
  return mutateRings([&](ClosedPolygon& ring) {
    auto oldSize = ring.size();
    erase_if(ring, [&](Vertex const& vertex) { return vertex.p == position; });
    return ring.size() != oldSize;
  });
}

bool MeshPrimitiveEditingProxy::removeEdge(uint32_t edgeIndex) {
  auto const& edge = mImpl->mesh.getEdge(edgeIndex);
  auto first = mImpl->mesh.getVertex(edge.getFirstVertex()).getPosition();
  auto second = mImpl->mesh.getVertex(edge.getSecondVertex()).getPosition();
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

bool MeshPrimitive::retainRing(uint32_t complexPolygonIndex, uint32_t ringIndex) {
  auto polygons = flattenTree();
  if (complexPolygonIndex >= polygons.size() || ringIndex >= polygons[complexPolygonIndex].size()) {
    return false;
  }
  MeshFilledRegion shell{polygons[complexPolygonIndex][ringIndex], {}};
  replaceTree({move(shell)});
  return true;
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

}  // namespace core
}  // namespace bw
