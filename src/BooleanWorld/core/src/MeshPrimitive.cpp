#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

#include <willpower/geometry/Edge.h>
#include <willpower/geometry/Polygon.h>
#include <willpower/geometry/Vertex.h>

#include "core/CoreException.h"
#include "core/MeshPrimitive.h"

namespace bw {
namespace core {

using namespace std;

namespace {

float twiceArea(ClosedPolygon const& ring) {
  float result = 0.0f;
  for (size_t i = 0; i < ring.size(); ++i) {
    auto const& a = ring[i].p;
    auto const& b = ring[(i + 1) % ring.size()].p;
    result += a.x * b.y - b.x * a.y;
  }
  return result;
}

bool containsPoint(ClosedPolygon const& ring, wp::Vector2 const& point) {
  bool inside = false;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    auto const& a = ring[i].p;
    auto const& b = ring[j].p;
    if (((a.y > point.y) != (b.y > point.y)) &&
        point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
      inside = !inside;
    }
  }
  return inside;
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
      reverse &= first[i].p ==
                 second[(start + second.size() - i) % second.size()].p;
    }
    if (forward || reverse) {
      return true;
    }
  }
  return false;
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
  vertices.reserve(ring.size());
  edgeData.reserve(ring.size() * 3);

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
  for (auto vertexIndex : mesh.getPolygon(polygonIndex).getOrderedVertexIndices()) {
    ring.emplace_back(mesh.getVertex(vertexIndex).getPosition());
  }
  return ring;
}

}  // namespace

MeshPrimitive::MeshPrimitive()
    : Primitive() {
}

MeshPrimitive::MeshPrimitive(Operation operation, FillRule fillType, vector<ComplexPolygon> const& polygons)
    : Primitive(operation, fillType, polygons) {
  generateVertices();
}

MeshPrimitive::MeshPrimitive(MeshPrimitive const& other) {
  copyFrom(other);
}

MeshPrimitive& MeshPrimitive::operator=(MeshPrimitive const& other) {
  copyFrom(other);
  return *this;
}

MeshPrimitive* MeshPrimitive::fromComplexPolygons(
    Operation operation,
    FillRule fillType,
    vector<ComplexPolygon> complexPolygons) {
  auto bounds = calculatePolygonBounds(complexPolygons);

  // Recentre and rescale vertices so that they are in unit space around the local origin
  auto const& pCentre = bounds.getCentre();
  auto halfSize = bounds.getHalfSize();
  auto scale = std::max(halfSize.x, halfSize.y);

  for (auto& complexPolygon : complexPolygons) {
    for (auto& polygon : complexPolygon) {
      auto numVertices = (uint32_t)polygon.size();
      for (uint32_t i = 0; i < numVertices; ++i) {
        polygon[i].p -= pCentre;
        polygon[i].p /= scale;
      }
    }
  }

  // Create new primitive
  auto p = new MeshPrimitive(operation, fillType, complexPolygons);

  p->setSize(scale * 2, scale * 2);
  p->setPosition(pCentre);

  // Set the scale and angle default values here, so that if we toggle them off/on, we don't lose the settings we
  // specified at creation.  Orbit angle and distance are not specified at creation so we can hardcode those defaults.
  {
    auto mutation = p->mutate();
    mutation.animation(VertexTransformer::Key::Scale).setDefaultStructure({{0.0f, 1.0f}, {1.0f, 1.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::Angle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitAngle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitDistance).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
  }

  return p;
}

unique_ptr<wp::geometry::Mesh> MeshPrimitive::createGeometryProxy() const {
  // A copy resets Primitive time to zero. Recalculate its animator caches and
  // vertices so the proxy never captures the currently displayed animation.
  MeshPrimitive restPose(*this);
  restPose.calculateAnimationValues();
  restPose.updateVertexPositions();

  auto mesh = make_unique<wp::geometry::Mesh>();
  vector<ClosedPolygon> rings;
  vector<uint32_t> polygonIndices;
  vector<uint32_t> explicitParents;
  vector<uint8_t> outerRings;

  for (auto const& complexPolygon : restPose.getVertices()) {
    uint32_t outerIndex = ~0u;
    for (size_t ringIndex = 0; ringIndex < complexPolygon.size(); ++ringIndex) {
      auto const& ring = complexPolygon[ringIndex];
      if (ring.size() < 3) {
        continue;
      }
      auto index = static_cast<uint32_t>(rings.size());
      if (ringIndex == 0) {
        outerIndex = index;
      }
      uint32_t coincidentRing = ~0u;
      for (uint32_t candidate = 0; candidate < rings.size(); ++candidate) {
        if (ringsCoincide(rings[candidate], ring)) {
          coincidentRing = candidate;
          break;
        }
      }
      rings.push_back(ring);
      polygonIndices.push_back(
          coincidentRing == ~0u
              ? addRing(*mesh, ring)
              : addRingSharingBoundary(*mesh, polygonIndices[coincidentRing]));
      explicitParents.push_back(ringIndex == 0 ? ~0u : outerIndex);
      outerRings.push_back(ringIndex == 0);
    }
  }

  // Storage is authoritative for the first level: Ring 0 is the outer and
  // every remaining Ring is its hole, even if another polygon happens to sit
  // between them geometrically.
  for (uint32_t child = 0; child < explicitParents.size(); ++child) {
    if (explicitParents[child] != ~0u) {
      mesh->addHoleToPolygon(
          polygonIndices[explicitParents[child]], polygonIndices[child]);
    }
  }

  // Filled islands are top-level storage polygons, not holes. Re-derive their
  // containment geometrically rather than from list adjacency. Willpower's
  // one-level hole model represents the result by leaving the island filled
  // and top-level; its coordinates inside the smallest containing hole make
  // point and triangulation queries recover the filled region.
  for (uint32_t island = 0; island < rings.size(); ++island) {
    if (!outerRings[island]) {
      continue;
    }
    auto smallestHoleArea = numeric_limits<float>::max();
    uint32_t containingHole = ~0u;
    for (uint32_t candidate = 0; candidate < rings.size(); ++candidate) {
      if (explicitParents[candidate] == ~0u) {
        continue;
      }
      auto area = abs(twiceArea(rings[candidate]));
      if (area < smallestHoleArea &&
          containsPoint(rings[candidate], rings[island].front().p)) {
        smallestHoleArea = area;
        containingHole = candidate;
      }
    }
    // The classification is intentionally represented by topology already
    // built above: a contained outer remains a live, non-hole polygon.
    if (containingHole != ~0u && mesh->getPolygon(polygonIndices[island]).isHole()) {
      throw CoreException("A filled MeshPrimitive island was converted to a hole.");
    }
  }
  return mesh;
}

void MeshPrimitive::updateFromGeometryProxy(wp::geometry::Mesh const& mesh) {
  // The rest-pose transform is affine. Sampling its origin and basis avoids
  // duplicating VertexTransformer's orientation/animation composition here.
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
    auto unscaled = wp::Vector2{
        (p.x * yAxis.y - p.y * yAxis.x) / determinant,
        (xAxis.x * p.y - xAxis.y * p.x) / determinant};
    return unscaled / getSize();
  };

  vector<ComplexPolygon> polygons;
  auto visit = [&](auto&& self, uint32_t polygonIndex, uint32_t depth) -> void {
    auto const& polygon = mesh.getPolygon(polygonIndex);
    if ((depth % 2) == 0) {
      ComplexPolygon complexPolygon;
      auto outer = readRing(mesh, polygonIndex);
      for (auto& vertex : outer) {
        vertex.p = toLocal(vertex.p);
      }
      complexPolygon.push_back(move(outer));
      for (auto holeIndex : polygon.getHoleIndices()) {
        auto hole = readRing(mesh, holeIndex);
        for (auto& vertex : hole) {
          vertex.p = toLocal(vertex.p);
        }
        complexPolygon.push_back(move(hole));
      }
      polygons.push_back(move(complexPolygon));
    }
    for (auto child : polygon.getHoleIndices()) {
      self(self, child, depth + 1);
    }
  };

  for (auto polygonIndex = mesh.getFirstPolygonIndex();
       !mesh.polygonIndexIterationFinished(polygonIndex);
       polygonIndex = mesh.getNextPolygonIndex(polygonIndex)) {
    if (!mesh.getPolygon(polygonIndex).isHole()) {
      visit(visit, polygonIndex, 0);
    }
  }
  setVertices(polygons);
}

void MeshPrimitive::copyFrom(MeshPrimitive const& other) {
  Primitive::copyFrom(other);
}

Primitive* MeshPrimitive::copy() const {
  return new MeshPrimitive(*this);
}

string MeshPrimitive::getType() const {
  return "Mesh";
}

string MeshPrimitive::getName() const {
  if (getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return "Ghost";
  } else {
    return "Mesh";
  }
}

void MeshPrimitive::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("meshPrimitive");
  {
    serializer->endMap();  // meshPrimitive
  }
}

bool MeshPrimitive::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  try {
    serializer->beginMap("meshPrimitive");
    {
      serializer->endMap();  // meshPrimitive
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  return true;
}

vector<ComplexPolygon> MeshPrimitive::generateVerticesImpl() {
  return mPolygons;
}

float MeshPrimitive::getRadius() const {
  return 1.0f;
}

}  // namespace core
}  // namespace bw