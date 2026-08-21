#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <stdexcept>

#include <willpower/geometry/Edge.h>
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

uint32_t addRingSharingBoundary(
    wp::geometry::Mesh& mesh, uint32_t sourcePolygonIndex) {
  wp::geometry::IndexVector edgeData;
  for (auto const& edge : mesh.getPolygon(sourcePolygonIndex).getEdges()) {
    edgeData.insert(edgeData.end(), {edge.v0, edge.v1, edge.index});
  }
  return mesh.addPolygon(wp::geometry::Polygon(edgeData));
}

uint32_t addRing(wp::geometry::Mesh& mesh, ClosedPolygon const& ring) {
  wp::geometry::IndexVector vertices;
  wp::geometry::IndexVector edgeData;
  for (auto const& vertex : ring) {
    vertices.push_back(mesh.addVertex(wp::geometry::Vertex(vertex.p)));
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto first = vertices[i];
    auto second = vertices[(i + 1) % vertices.size()];
    auto edge = mesh.addEdge(wp::geometry::Edge(first, second));
    edgeData.insert(edgeData.end(), {first, second, edge});
  }
  return mesh.addPolygon(wp::geometry::Polygon(edgeData));
}

ClosedPolygon readRing(wp::geometry::Mesh const& mesh, uint32_t polygonIndex) {
  ClosedPolygon ring;
  for (auto vertex : mesh.getPolygon(polygonIndex).getOrderedVertexIndices()) {
    ring.emplace_back(mesh.getVertex(vertex).getPosition());
  }
  return ring;
}

vector<MeshFilledRegion> inferTree(vector<ComplexPolygon> const& polygons) {
  vector<MeshFilledRegion> regions;
  regions.reserve(polygons.size());
  // Proxy polygons are a derived list of independently filled regions, so
  // validate each shallow entry before rebuilding their compatibility-only
  // hierarchy. The public shallow converter deliberately does not do this.
  for (auto const& polygon : polygons) {
    auto one = shallowTree({polygon});
    regions.push_back(move(one.front()));
  }
  vector<int> parents(regions.size(), -1);
  vector<size_t> parentHoles(regions.size());
  for (size_t child = 0; child < regions.size(); ++child) {
    double smallest = numeric_limits<double>::max();
    for (size_t candidate = 0; candidate < regions.size(); ++candidate) {
      if (candidate == child) continue;
      for (size_t hole = 0; hole < regions[candidate].holes.size(); ++hole) {
        auto const& boundary = regions[candidate].holes[hole].ring;
        if (ringContainedBy(regions[child].ring, boundary) && abs(twiceArea(boundary)) < smallest) {
          parents[child] = int(candidate);
          parentHoles[child] = hole;
          smallest = abs(twiceArea(boundary));
        }
      }
    }
  }
  vector<vector<size_t>> children(regions.size());
  for (size_t child = 0; child < regions.size(); ++child) {
    if (parents[child] >= 0) children[size_t(parents[child])].push_back(child);
  }
  auto build = [&](auto&& self, size_t index) -> MeshFilledRegion {
    auto result = regions[index];
    for (auto child : children[index]) {
      result.holes[parentHoles[child]].islands.push_back(self(self, child));
    }
    return result;
  };
  vector<MeshFilledRegion> roots;
  for (size_t i = 0; i < regions.size(); ++i) {
    if (parents[i] < 0) roots.push_back(build(build, i));
  }
  normalizeAndValidateTree(roots);
  return roots;
}

}  // namespace

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

unique_ptr<wp::geometry::Mesh> MeshPrimitive::createGeometryProxy() const {
  MeshPrimitive restPose(*this);
  restPose.calculateAnimationValues();
  restPose.updateVertexPositions();
  auto mesh = make_unique<wp::geometry::Mesh>();
  vector<ClosedPolygon> rings;
  vector<uint32_t> indices;
  auto addCompatibleRing = [&](ClosedPolygon const& ring) {
    for (size_t candidate = 0; candidate < rings.size(); ++candidate) {
      if (ringsCoincide(rings[candidate], ring)) {
        rings.push_back(ring);
        indices.push_back(addRingSharingBoundary(*mesh, indices[candidate]));
        return indices.back();
      }
    }
    rings.push_back(ring);
    indices.push_back(addRing(*mesh, ring));
    return indices.back();
  };
  for (auto const& polygon : restPose.getVertices()) {
    if (polygon.empty()) continue;
    auto outer = addCompatibleRing(polygon.front());
    for (size_t ring = 1; ring < polygon.size(); ++ring) {
      auto hole = addCompatibleRing(polygon[ring]);
      mesh->addHoleToPolygon(outer, hole);
    }
  }
  return mesh;
}

void MeshPrimitive::updateFromGeometryProxy(wp::geometry::Mesh const& mesh) {
  MeshPrimitive restPose(*this);
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
           getSize();
  };
  vector<ComplexPolygon> polygons;
  for (auto index = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(index);
       index = mesh.getNextPolygonIndex(index)) {
    auto const& polygon = mesh.getPolygon(index);
    if (polygon.isHole()) continue;
    ComplexPolygon complex{readRing(mesh, index)};
    for (auto hole : polygon.getHoleIndices()) complex.push_back(readRing(mesh, hole));
    for (auto& ring : complex)
      for (auto& vertex : ring) vertex.p = toLocal(vertex.p);
    polygons.push_back(move(complex));
  }
  auto candidate = inferTree(polygons);
  replaceTree(move(candidate));  // candidate is complete before authority changes
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
  Primitive::serializeImpl(serializer, workData);
  auto writeRing = [&](ClosedPolygon const& ring) {
    serializer->beginArray("vertices");
    for (auto const& vertex : ring) {
      serializer->beginMap("vertex");
      serializer->writeVector2("p", vertex.p);
      serializer->endMap();
    }
    serializer->endArray();
  };
  auto writeFilled = [&](auto&& self, MeshFilledRegion const& filled) -> void {
    serializer->beginMap("filledRegion");
    writeRing(filled.ring);
    serializer->beginArray("holes");
    for (auto const& hole : filled.holes) {
      serializer->beginMap("hole");
      writeRing(hole.ring);
      serializer->beginArray("islands");
      for (auto const& island : hole.islands) self(self, island);
      serializer->endArray();
      serializer->endMap();
    }
    serializer->endArray();
    serializer->endMap();
  };
  serializer->beginMap("meshPrimitive");
  serializer->beginArray("shells");
  for (auto const& shell : mShells) writeFilled(writeFilled, shell);
  serializer->endArray();
  serializer->endMap();
}

bool MeshPrimitive::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) return false;
  vector<MeshFilledRegion> candidate;
  try {
    auto readRing = [&]() {
      ClosedPolygon ring;
      serializer->beginArray("vertices");
      while (serializer->nextArrayItem()) {
        serializer->beginMap("vertex");
        ring.emplace_back(serializer->readVector2("p"));
        serializer->endMap();
      }
      serializer->endArray();
      return ring;
    };
    auto readFilled = [&](auto&& self) -> MeshFilledRegion {
      serializer->beginMap("filledRegion");
      MeshFilledRegion filled{readRing(), {}};
      serializer->beginArray("holes");
      while (serializer->nextArrayItem()) {
        serializer->beginMap("hole");
        MeshHole hole{readRing(), {}};
        serializer->beginArray("islands");
        while (serializer->nextArrayItem()) hole.islands.push_back(self(self));
        serializer->endArray();
        serializer->endMap();
        filled.holes.push_back(move(hole));
      }
      serializer->endArray();
      serializer->endMap();
      return filled;
    };
    serializer->beginMap("meshPrimitive");
    serializer->beginArray("shells");
    while (serializer->nextArrayItem()) candidate.push_back(readFilled(readFilled));
    serializer->endArray();
    serializer->endMap();
    normalizeAndValidateTree(candidate);
  } catch (exception const& error) {
    addDeserializationError(error.what());
    return false;
  }
  mShells = move(candidate);
  mPolygons = flattenTree();
  updateVertexPositions();
  return true;
}

float MeshPrimitive::getRadius() const { return 1.0f; }

}  // namespace core
}  // namespace bw
