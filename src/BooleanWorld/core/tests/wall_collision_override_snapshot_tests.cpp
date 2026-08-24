#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <core/ArrangementWorldDataGenerator.h>
#include <core/MeshPrimitive.h>
#include <core/RectanglePolygon.h>

namespace {

using bw::core::ClosedPolygon;
using bw::core::ComplexPolygon;
using bw::core::ConvertPrimitiveToContours;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using bw::core::RectanglePolygon;
using bw::core::SnapshotPrimitives;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ClosedPolygon ring(float minX, float minY, float maxX, float maxY) {
  return {{{minX, minY}}, {{maxX, minY}}, {{maxX, maxY}}, {{minX, maxY}}};
}

ComplexPolygon rectangle(float minX, float minY, float maxX, float maxY) {
  return {ring(minX, minY, maxX, maxY)};
}

// Locates the (contour, edge) index in `contours` whose directed edge runs
// from world position `a` to world position `b` (in either direction - Mesh
// contour winding relative to the authored Ring is not guaranteed here).
// Returns {~0u, ~0u} if not found.
std::pair<size_t, size_t> findEdge(
    std::vector<bw::core::arr::Contour> const& contours,
    wp::Vector2 const& a,
    wp::Vector2 const& b) {
  auto fa = bw::core::arr::FixedPointVertex{
      bw::core::arr::ToFixedPointCoordinate(a.x),
      bw::core::arr::ToFixedPointCoordinate(a.y)};
  auto fb = bw::core::arr::FixedPointVertex{
      bw::core::arr::ToFixedPointCoordinate(b.x),
      bw::core::arr::ToFixedPointCoordinate(b.y)};
  for (size_t c = 0; c < contours.size(); ++c) {
    auto const& contour = contours[c];
    auto n = contour.size();
    for (size_t i = 0; i < n; ++i) {
      auto j = (i + 1) % n;
      if ((contour[i] == fa && contour[j] == fb) ||
          (contour[i] == fb && contour[j] == fa)) {
        return {c, i};
      }
    }
  }
  return {~size_t(0), ~size_t(0)};
}

void meshExternalEdgeOverrideIsExtractedAtTheRightIndex() {
  for (bool authoredValue : {true, false}) {
    auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
        Primitive::Operation::Union, {rectangle(0.0f, 0.0f, 10.0f, 10.0f)}));
    auto proxy = primitive->createEditingProxy();
    auto edgeIndex = proxy->getFirstEdgeIndex();
    require(proxy->isEdgeCollisionEditable(edgeIndex),
            "the single-shell fixture's edge was not External/editable");
    auto edge = proxy->getEdge(edgeIndex);
    auto aPos = proxy->getVertex(edge.getFirstVertex()).getPosition();
    auto bPos = proxy->getVertex(edge.getSecondVertex()).getPosition();

    require(proxy->setEdgeCollides(edgeIndex, authoredValue),
            "setEdgeCollides was refused on an External edge");
    proxy->commitTo(*primitive);

    auto converted = ConvertPrimitiveToContours(*primitive);
    auto [c, i] = findEdge(converted.contours, aPos, bPos);
    require(c != ~size_t(0), "the authored edge could not be located in the converted contours");
    require(c < converted.edgeOverrides.size() && i < converted.edgeOverrides[c].size(),
            "edgeOverrides did not cover the authored edge's index");
    require(converted.edgeOverrides[c][i].has_value(),
            "an External edge did not produce an override value");
    require(*converted.edgeOverrides[c][i] == authoredValue,
            "the extracted override did not match the authored collides value");

    // SnapshotPrimitives must carry the same value through into the
    // ArrangementPrimitive it builds.
    std::vector<Primitive*> primitives{primitive.get()};
    auto snapshot = SnapshotPrimitives(primitives);
    require(snapshot.size() == 1, "SnapshotPrimitives did not snapshot the primitive");
    auto const& arrangementPrimitive = snapshot.front();
    require(c < arrangementPrimitive.contourEdgeOverrides.size() &&
                i < arrangementPrimitive.contourEdgeOverrides[c].size(),
            "SnapshotPrimitives did not carry contourEdgeOverrides through");
    require(arrangementPrimitive.contourEdgeOverrides[c][i].has_value() &&
                *arrangementPrimitive.contourEdgeOverrides[c][i] == authoredValue,
            "SnapshotPrimitives lost or altered the authored override value");
  }
}

void meshInternalEdgeProducesNoOverride() {
  // Two Shells sharing a boundary weld into one Internal edge along x = 0,
  // per the #244 fixture (mesh_primitive_geometry_proxy_tests.cpp).
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{ring(-2, -1, 0, 1), {}}, {ring(0, -1, 2, 1), {}}}));

  auto converted = ConvertPrimitiveToContours(*primitive);
  require(converted.contours.size() == 2,
          "the two-shell fixture did not produce two contours");

  // The shared boundary runs from (0, -1) to (0, 1) - locate it and confirm
  // it carries no override, in contrast to the outer (External) edges.
  auto [c, i] = findEdge(converted.contours, {0.0f, -1.0f}, {0.0f, 1.0f});
  require(c != ~size_t(0), "the shared internal edge could not be located");
  bool hasOverride = c < converted.edgeOverrides.size() &&
                      i < converted.edgeOverrides[c].size() &&
                      converted.edgeOverrides[c][i].has_value();
  require(!hasOverride, "an Internal edge produced a collides override");

  // An outer (External) edge on the same primitive still gets one, sourced
  // from its default-true authored state.
  auto [oc, oi] = findEdge(converted.contours, {-2.0f, -1.0f}, {0.0f, -1.0f});
  require(oc != ~size_t(0), "an outer External edge could not be located");
  require(oc < converted.edgeOverrides.size() && oi < converted.edgeOverrides[oc].size() &&
              converted.edgeOverrides[oc][oi].has_value() &&
              *converted.edgeOverrides[oc][oi] == true,
          "an untouched External edge did not default to a collides = true override");
}

void nonMeshPrimitiveProducesNoOverridesRegardlessOfVertexData() {
  auto rectangle = std::unique_ptr<RectanglePolygon>(new RectanglePolygon(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 1.0f));
  rectangle->setSize(10.0f, 10.0f);
  rectangle->updateVertexPositions();

  auto converted = ConvertPrimitiveToContours(*rectangle);
  require(!converted.contours.empty(), "the rectangle fixture produced no contours at all");
  require(converted.edgeOverrides.empty(),
          "a non-MeshPrimitive produced non-empty edgeOverrides");

  std::vector<Primitive*> primitives{rectangle.get()};
  auto snapshot = SnapshotPrimitives(primitives);
  require(snapshot.size() == 1 && snapshot.front().contourEdgeOverrides.empty(),
          "SnapshotPrimitives produced overrides for a non-MeshPrimitive");
}

}  // namespace

int main() {
  try {
    meshExternalEdgeOverrideIsExtractedAtTheRightIndex();
    meshInternalEdgeProducesNoOverride();
    nonMeshPrimitiveProducesNoOverridesRegardlessOfVertexData();
    std::cout << "ConvertPrimitiveToContours/SnapshotPrimitives extract the mesh wall collision override\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
