#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <core/ArrangementWorldDataGenerator.h>
#include <core/ArrangementWorldData.h>
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

    require(proxy->setEdgeCollisionOverride(edgeIndex, authoredValue),
            "setting a collision override was refused on an External edge");
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

void meshExternalEdgeVisibleOverrideIsExtractedAtTheRightIndex() {
  for (bool authoredValue : {true, false}) {
    auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
        Primitive::Operation::Union, {rectangle(0.0f, 0.0f, 10.0f, 10.0f)}));
    auto proxy = primitive->createEditingProxy();
    auto edgeIndex = proxy->getFirstEdgeIndex();
    require(proxy->isEdgeVisibilityEditable(edgeIndex),
            "the single-shell fixture's edge was not External/visibility-editable");
    auto edge = proxy->getEdge(edgeIndex);
    auto aPos = proxy->getVertex(edge.getFirstVertex()).getPosition();
    auto bPos = proxy->getVertex(edge.getSecondVertex()).getPosition();

    require(proxy->setEdgeVisible(edgeIndex, authoredValue),
            "setEdgeVisible was refused on an External edge");
    proxy->commitTo(*primitive);

    auto converted = ConvertPrimitiveToContours(*primitive);
    auto [c, i] = findEdge(converted.contours, aPos, bPos);
    require(c != ~size_t(0), "the authored edge could not be located in the converted contours");
    require(c < converted.edgeVisibleOverrides.size() && i < converted.edgeVisibleOverrides[c].size(),
            "edgeVisibleOverrides did not cover the authored edge's index");
    require(converted.edgeVisibleOverrides[c][i].has_value(),
            "an External edge did not produce a visible override value");
    require(*converted.edgeVisibleOverrides[c][i] == authoredValue,
            "the extracted override did not match the authored visible value");

    std::vector<Primitive*> primitives{primitive.get()};
    auto snapshot = SnapshotPrimitives(primitives);
    require(snapshot.size() == 1, "SnapshotPrimitives did not snapshot the primitive");
    auto const& arrangementPrimitive = snapshot.front();
    require(c < arrangementPrimitive.contourEdgeVisibleOverrides.size() &&
                i < arrangementPrimitive.contourEdgeVisibleOverrides[c].size(),
            "SnapshotPrimitives did not carry contourEdgeVisibleOverrides through");
    require(arrangementPrimitive.contourEdgeVisibleOverrides[c][i].has_value() &&
                *arrangementPrimitive.contourEdgeVisibleOverrides[c][i] == authoredValue,
            "SnapshotPrimitives lost or altered the authored visible override value");
  }
}

void imageNormalMapPropagatesToItsSurvivingBorderWall() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union, {rectangle(0, 0, 10, 10)}));
  auto proxy = primitive->createEditingProxy();
  auto edgeIndex = proxy->getFirstEdgeIndex();
  auto image = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 4.0f, 0.6f);
  require(proxy->setEdgeNormalMapOverride(edgeIndex, image),
          "External edge refused its Image normal map");
  proxy->commitTo(*primitive);

  std::vector<Primitive*> primitives{primitive.get()};
  auto snapshot = SnapshotPrimitives(primitives);
  bool snapshotCarriesImage = false;
  for (auto const& contour : snapshot.front().contourEdgeNormalMapOverrides) {
    for (auto const& value : contour) {
      snapshotCarriesImage |= value.has_value() && *value == image;
    }
  }
  require(snapshotCarriesImage,
          "SnapshotPrimitives lost the authored Image normal map");

  auto arrangement = bw::core::arr::BuildArrangement(snapshot);
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  size_t mappedBorders = 0;
  for (auto const& wall : walls) {
    mappedBorders += wall.kind == bw::core::arr::ArrangementWallKind::Border &&
                     wall.normalMapOverride == image;
  }
  require(mappedBorders == 1,
          "the selected edge's Image did not reach exactly one surviving Border ArrangementWall");
}

void imageWallMaskPropagatesToItsSurvivingBorderWall() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union, {rectangle(0, 0, 10, 10)}));
  auto proxy = primitive->createEditingProxy();
  auto edgeIndex = proxy->getFirstEdgeIndex();
  auto image = bw::core::WallMaskOverride::image(
      "mask/blend.png", 1,
      bw::core::WallMaskOverride::BlendParameters{
          0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f});
  require(proxy->setEdgeWallMaskOverride(edgeIndex, image),
          "External edge refused its Image wall mask");
  proxy->commitTo(*primitive);

  std::vector<Primitive*> primitives{primitive.get()};
  auto snapshot = SnapshotPrimitives(primitives);
  bool snapshotCarriesImage = false;
  for (auto const& contour : snapshot.front().contourEdgeWallMaskOverrides) {
    for (auto const& value : contour) {
      snapshotCarriesImage |= value.has_value() && *value == image;
    }
  }
  require(snapshotCarriesImage,
          "SnapshotPrimitives lost the authored Image wall mask");

  auto arrangement = bw::core::arr::BuildArrangement(snapshot);
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  size_t mappedBorders = 0;
  for (auto const& wall : walls) {
    mappedBorders += wall.kind == bw::core::arr::ArrangementWallKind::Border &&
                     wall.wallMaskOverride == image;
  }
  require(mappedBorders == 1,
          "the selected edge's Image mask did not reach exactly one surviving Border ArrangementWall");
}

void borderSideZonesFollowAuthoredEdges() {
  using namespace bw::core;
  // Exercise both the unbounded exterior and a bounded Hole, both side
  // orientations, explicit equal IDs, dormant values and hidden walls.
  for (auto zone : {ZoneId::Euclidean, ZoneId::NegativeSpace}) {
    for (bool collides : {false, true}) {
      for (bool visible : {false, true}) {
        auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
            Primitive::Operation::Union,
            {{ring(-10, -10, 10, 10), {{ring(-3, -3, 3, 3), {}}}}}));
        auto proxy = mesh->createEditingProxy();
        for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
             edge = proxy->getNextEdgeIndex(edge)) {
          if (!proxy->isEdgeCollisionEditable(edge)) continue;
          require(proxy->setEdgeOtherZone(edge, zone) &&
                      proxy->setEdgeCollisionOverride(edge, collides) &&
                      proxy->setEdgeVisible(edge, visible),
                  "could not author Hole/outer edge");
        }
        proxy->commitTo(*mesh);
        auto converted = ConvertPrimitiveToContours(*mesh);
        for (auto const& contour : converted.edgeOtherZones)
          for (auto value : contour)
            require(!value || value == zone, "contour conversion lost Other Zone");
        auto inputs = SnapshotPrimitives({mesh.get()});
        require(inputs.front().contourEdgeOtherZones == converted.edgeOtherZones,
                "immutable input lost Other Zone");
        auto arrangement = arr::BuildArrangement(inputs);
        ArrangementWorldData const runtime(
            arrangement, wp::BoundingBox({-20, -20}, {20, 20}), 4.0f);
        auto const& walls = runtime.getWalls();
        require(walls.size() == 8, "expected outer and Hole Borders");
        bool leftSolid = false, rightSolid = false;
        for (auto const& wall : walls) {
          require(wall.visible == visible, "Zone altered visibility");
          require(wall.sideZones.has_value() == !collides,
                  "only non-colliding Borders should carry side Zones");
          auto const& edge = arrangement->edges[wall.edge];
          if (!collides) {
            for (int side = 0; side < 2; ++side) {
              bool solid = arrangement->faces[edge.face[side]].solid;
              require((*wall.sideZones)[side] == (solid ? ZoneId::Euclidean : zone),
                      "Border Zone assigned to wrong geometric side");
            }
          }
          leftSolid |= arrangement->faces[edge.face[0]].solid;
          rightSolid |= arrangement->faces[edge.face[1]].solid;
        }
        require(leftSolid && rightSolid, "fixture must exercise both orientations");
      }
    }
  }
}

void otherZoneUsesPropertyPrecedenceIndependentlyOfCollision() {
  using namespace bw::core;
  auto make = [](ZoneId zone, bool collides) {
    auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
        Primitive::Operation::Union, {rectangle(0, 0, 10, 10)}));
    auto proxy = mesh->createEditingProxy();
    for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge)) {
      proxy->setEdgeOtherZone(edge, zone);
      proxy->setEdgeCollisionOverride(edge, collides);
    }
    proxy->commitTo(*mesh);
    return mesh;
  };
  auto lower = make(ZoneId::NegativeSpace, false);
  auto higher = make(ZoneId::Euclidean, true);
  for (bool transparent : {false, true}) {
    auto inputs = SnapshotPrimitives({higher.get(), lower.get()}, {20, 10});
    inputs[0].contributesProperties = !transparent;
    auto arrangement = arr::BuildArrangement(inputs);
    auto walls = arr::BuildArrangementWalls(*arrangement);
    require(walls.size() == 4, "coincident fixture lost Borders");
    for (auto const& wall : walls) {
      require(wall.sideZones.has_value(), "collision false must dominate independently");
      auto const& edge = arrangement->edges[wall.edge];
      int emptySide = arrangement->faces[edge.face[0]].solid ? 1 : 0;
      require((*wall.sideZones)[emptySide] ==
                  (transparent ? ZoneId::NegativeSpace : ZoneId::Euclidean),
              "Other Zone ignored property precedence/transparency");
    }
  }

  // Adjacent solids with different heights generate Steps, not Zone boundaries.
  auto adjacent = make(ZoneId::NegativeSpace, false);
  auto inputs = SnapshotPrimitives({lower.get(), adjacent.get()});
  for (auto& contour : inputs[1].contours)
    for (auto& vertex : contour) vertex.x += arr::ToFixedPointCoordinate(10);
  inputs[1].properties.floorZ = 1.0f;
  auto arrangement = arr::BuildArrangement(inputs);
  size_t steps = 0;
  for (auto const& wall : arr::BuildArrangementWalls(*arrangement)) {
    if (wall.kind != arr::ArrangementWallKind::Border) {
      ++steps;
      require(!wall.sideZones, "Step became a Zone boundary");
    }
  }
  require(steps != 0, "fixture produced no Steps");
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
  require(!converted.edgeOtherZones[c][i], "Internal edge produced Other Zone");
  bool hasVisibleOverride = c < converted.edgeVisibleOverrides.size() &&
                            i < converted.edgeVisibleOverrides[c].size() &&
                            converted.edgeVisibleOverrides[c][i].has_value();
  require(!hasVisibleOverride, "an Internal edge produced a visible override");
  bool hasNormalMap = c < converted.edgeNormalMapOverrides.size() &&
                      i < converted.edgeNormalMapOverrides[c].size() &&
                      converted.edgeNormalMapOverrides[c][i].has_value();
  require(!hasNormalMap, "an Internal edge produced a normal-map override");
  bool hasWallMask = c < converted.edgeWallMaskOverrides.size() &&
                     i < converted.edgeWallMaskOverrides[c].size() &&
                     converted.edgeWallMaskOverrides[c][i].has_value();
  require(!hasWallMask, "an Internal edge produced a wall-mask override");

  // An untouched outer (External) edge leaves collision unset so generation
  // can derive collision from the wall kind and physical constraints.
  auto [oc, oi] = findEdge(converted.contours, {-2.0f, -1.0f}, {0.0f, -1.0f});
  require(oc != ~size_t(0), "an outer External edge could not be located");
  require(oc < converted.edgeOverrides.size() &&
              oi < converted.edgeOverrides[oc].size() &&
              !converted.edgeOverrides[oc][oi].has_value(),
          "an untouched External edge produced a collision override");
  require(oc < converted.edgeVisibleOverrides.size() && oi < converted.edgeVisibleOverrides[oc].size() &&
              converted.edgeVisibleOverrides[oc][oi].has_value() &&
              *converted.edgeVisibleOverrides[oc][oi] == true,
          "an untouched External edge did not default to a visible = true override");
}

void nonMeshPrimitiveProducesNoOverridesRegardlessOfVertexData() {
  auto rectangle = std::unique_ptr<RectanglePolygon>(new RectanglePolygon(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 1.0f));
  rectangle->setSize(10.0f, 10.0f);
  rectangle->updateVertexPositions();

  auto converted = ConvertPrimitiveToContours(*rectangle);
  require(!converted.contours.empty(), "the rectangle fixture produced no contours at all");
  require(converted.edgeOtherZones.empty(), "non-Mesh produced Other Zones");
  require(converted.edgeOverrides.empty(),
          "a non-MeshPrimitive produced non-empty edgeOverrides");
  require(converted.edgeVisibleOverrides.empty(),
          "a non-MeshPrimitive produced non-empty edgeVisibleOverrides");
  require(converted.edgeNormalMapOverrides.empty(),
          "a non-MeshPrimitive produced normal-map overrides");
  require(converted.edgeWallMaskOverrides.empty(),
          "a non-MeshPrimitive produced wall-mask overrides");

  std::vector<Primitive*> primitives{rectangle.get()};
  auto snapshot = SnapshotPrimitives(primitives);
  require(snapshot.size() == 1 && snapshot.front().contourEdgeOverrides.empty(),
          "SnapshotPrimitives produced overrides for a non-MeshPrimitive");
  require(snapshot.front().contourEdgeVisibleOverrides.empty(),
          "SnapshotPrimitives produced visible overrides for a non-MeshPrimitive");
  require(snapshot.front().contourEdgeWallMaskOverrides.empty(),
          "SnapshotPrimitives produced wall-mask overrides for a non-MeshPrimitive");
}

}  // namespace

int main() {
  try {
    meshExternalEdgeOverrideIsExtractedAtTheRightIndex();
    meshExternalEdgeVisibleOverrideIsExtractedAtTheRightIndex();
    imageNormalMapPropagatesToItsSurvivingBorderWall();
    imageWallMaskPropagatesToItsSurvivingBorderWall();
    borderSideZonesFollowAuthoredEdges();
    otherZoneUsesPropertyPrecedenceIndependentlyOfCollision();
    meshInternalEdgeProducesNoOverride();
    nonMeshPrimitiveProducesNoOverridesRegardlessOfVertexData();
    std::cout << "ConvertPrimitiveToContours/SnapshotPrimitives extract the mesh wall collision override\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
