#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <core/ChipGenerationParameters.h>
#include <core/PrimitivePropertySet.h>
#include <core/Stats.h>
#include <core/WedgeGenerationParameters.h>

namespace {
using bw::core::ArrangementWorldData;
using bw::core::Primitive;
using bw::core::WedgeGenerationParameters;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;
using bw::core::arr::DetailSurfaceKind;
using bw::core::arr::DetailTriangleKind;

constexpr int64_t U = 1000;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float tolerance = 0.01f) {
  return std::abs(a - b) <= tolerance;
}

Contour rectangle(
    int64_t x0 = -10, int64_t y0 = -8,
    int64_t x1 = 10, int64_t y1 = 8) {
  return {{x0 * U, y0 * U}, {x1 * U, y0 * U},
          {x1 * U, y1 * U}, {x0 * U, y1 * U}};
}

ArrangementPrimitive region(
    std::vector<Contour> contours,
    std::vector<std::optional<bool>> visibility = {}) {
  bw::core::PrimitivePropertySet properties;
  properties.floorZ = 0.0f;
  properties.ceilingZ = 10.0f;
  properties.ceilingMaterialId = "ceiling.fixture";
  return {std::move(contours), Primitive::Operation::Union,
          Primitive::FillRule::EvenOdd, 1, 0, properties, {},
          visibility.empty()
              ? std::vector<std::vector<std::optional<bool>>>{}
              : std::vector<std::vector<std::optional<bool>>>{visibility}};
}

ArrangementPrimitive room(
    Contour contour = rectangle(),
    std::vector<std::optional<bool>> visibility = {}) {
  return region({std::move(contour)}, std::move(visibility));
}

WedgeGenerationParameters fixedWedge(
    float reach = 6.0f, float drop = 3.0f, float depth = 2.0f) {
  return {true, reach, reach, drop, drop, depth, depth};
}

ArrangementWorldData snapshot(
    std::vector<ArrangementPrimitive> primitives,
    WedgeGenerationParameters const& settings = {},
    bw::core::ChipGenerationParameters const& chipSettings = {}) {
  for (auto& primitive : primitives) {
    primitive.chipParameters = chipSettings;
  }
  return {bw::core::arr::BuildArrangement(primitives),
          wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}),
          16.0f, 8.0f, nullptr, settings};
}

std::vector<std::array<float, 3>> wedgePositions(
    ArrangementWorldData const& data) {
  std::vector<std::array<float, 3>> result;
  for (auto const& triangle : data.getDetail().getTriangles()) {
    if (triangle.kind != DetailTriangleKind::WedgeFacet) continue;
    for (auto const& vertex : triangle.v) result.push_back(vertex.position);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::array<float, 3>> chipAndRemainderPositions(
    ArrangementWorldData const& data) {
  std::vector<std::array<float, 3>> result;
  for (auto const& triangle : data.getDetail().getTriangles()) {
    if (triangle.kind == DetailTriangleKind::WedgeFacet) continue;
    for (auto const& vertex : triangle.v) result.push_back(vertex.position);
  }
  std::sort(result.begin(), result.end());
  return result;
}

struct Dimensions {
  float reach{};
  float drop{};
  float depth{};
};

bool triangleBelongsToWedge(
    bw::core::arr::DetailTriangle const& triangle,
    wp::Vector2 const& midpoint,
    bool atTop = true) {
  auto topZ = std::max(
      {triangle.v[0].position[2], triangle.v[1].position[2],
       triangle.v[2].position[2]});
  auto bottomZ = std::min(
      {triangle.v[0].position[2], triangle.v[1].position[2],
       triangle.v[2].position[2]});
  return std::any_of(
      triangle.v.begin(), triangle.v.end(), [&](auto const& vertex) {
        return near(vertex.position[0], midpoint.x) &&
               near(vertex.position[1], midpoint.y) &&
               (atTop ? vertex.position[2] < topZ - 0.01f
                      : vertex.position[2] > bottomZ + 0.01f);
      });
}

bool hasWedgeAt(
    ArrangementWorldData const& data,
    wp::Vector2 const& midpoint,
    bool atTop = true) {
  return std::any_of(
      data.getDetail().getTriangles().begin(),
      data.getDetail().getTriangles().end(), [&](auto const& triangle) {
        return triangle.kind == DetailTriangleKind::WedgeFacet &&
               triangleBelongsToWedge(triangle, midpoint, atTop);
      });
}

Dimensions dimensionsAt(
    ArrangementWorldData const& data,
    wp::Vector2 const& midpoint,
    bool atTop = true) {
  auto const& triangles = data.getDetail().getTriangles();
  auto sourceKind = atTop ? DetailSurfaceKind::CeilingOfFace
                          : DetailSurfaceKind::FloorOfFace;
  std::map<std::array<float, 3>, int> globalOccurrences;
  for (auto const& triangle : triangles) {
    if (triangle.kind != DetailTriangleKind::WedgeFacet ||
        triangle.source.kind != sourceKind) {
      continue;
    }
    for (auto const& vertex : triangle.v) {
      ++globalOccurrences[vertex.position];
    }
  }
  std::set<std::array<float, 3>> positions;
  std::vector<bool> selected(triangles.size());
  for (size_t i = 0; i < triangles.size(); ++i) {
    if (triangles[i].kind == DetailTriangleKind::WedgeFacet &&
        triangles[i].source.kind == sourceKind &&
        triangleBelongsToWedge(triangles[i], midpoint, atTop)) {
      selected[i] = true;
      for (auto const& vertex : triangles[i].v) {
        positions.insert(vertex.position);
      }
    }
  }
  bool added = true;
  while (added) {
    added = false;
    for (size_t i = 0; i < triangles.size(); ++i) {
      if (selected[i] || triangles[i].kind != DetailTriangleKind::WedgeFacet ||
          triangles[i].source.kind != sourceKind) {
        continue;
      }
      auto connected = std::any_of(
          triangles[i].v.begin(), triangles[i].v.end(), [&](auto const& vertex) {
            auto found = globalOccurrences.find(vertex.position);
            return positions.contains(vertex.position) &&
                   found != globalOccurrences.end() && found->second == 4;
          });
      if (!connected) continue;
      selected[i] = true;
      added = true;
      for (auto const& vertex : triangles[i].v) {
        positions.insert(vertex.position);
      }
    }
  }

  std::map<std::array<float, 3>, int> occurrences;
  for (size_t i = 0; i < triangles.size(); ++i) {
    if (!selected[i]) continue;
    for (auto const& vertex : triangles[i].v) {
      ++occurrences[vertex.position];
    }
  }
  require(
      positions.size() == 6,
      "a Wedge at (" + std::to_string(midpoint.x) + ", " +
          std::to_string(midpoint.y) + ") exposed " +
          std::to_string(positions.size()) + " vertices instead of six");
  auto heightExtreme = [&](auto comparison) {
    return std::min_element(
        positions.begin(), positions.end(), comparison)->at(2);
  };
  auto arrisZ = atTop
                    ? heightExtreme(
                          [](auto const& a, auto const& b) {
                            return a[2] > b[2];
                          })
                    : heightExtreme(
                          [](auto const& a, auto const& b) {
                            return a[2] < b[2];
                          });
  std::vector<std::array<float, 3>> endpoints;
  std::array<float, 3> projected{};
  std::array<float, 3> dropped{};
  for (auto const& [position, count] : occurrences) {
    if (near(position[0], midpoint.x) && near(position[1], midpoint.y) &&
        !near(position[2], arrisZ)) {
      dropped = position;
    } else if (near(position[2], arrisZ) && count == 2) {
      projected = position;
    } else if (near(position[2], arrisZ) && count == 3) {
      endpoints.push_back(position);
    }
  }
  require(endpoints.size() == 2,
          "a Wedge did not have two distinct reach endpoints");
  auto endpointDistance = std::hypot(
      endpoints[0][0] - endpoints[1][0],
      endpoints[0][1] - endpoints[1][1]);
  return {endpointDistance, std::abs(arrisZ - dropped[2]),
          std::hypot(projected[0] - midpoint.x, projected[1] - midpoint.y)};
}

void disabledAndEligibilityContract() {
  auto disabled = snapshot({room()});
  require(disabled.getDetail().getWedgeCount() == 0 &&
              disabled.getDetail().getTriangles().empty(),
          "disabled Wedge settings generated detail");

  auto enabled = snapshot({room()}, fixedWedge());
  require(enabled.getDetail().getWedgeCount() == 16,
          "edge and Corner Wedges were not generated at floor and ceiling");
  require(enabled.getDetail().getTriangles().size() == 56,
          "edge or Corner Wedges emitted unexpected exposed geometry");
  require(enabled.getDetail().getSuppressed().empty(),
          "additive Wedges suppressed an attachment surface");

  auto hidden = snapshot(
      {room(rectangle(), {false, true, true, true})}, fixedWedge());
  require(hidden.getDetail().getWedgeCount() == 10,
          "an invisible Border wall generated an attached edge or Corner Wedge");

  auto raised = room(rectangle(-4, -3, 4, 3));
  raised.priority = 2;
  raised.primitiveIndex = 1;
  raised.properties.floorZ = 4.0f;
  auto withSteps = snapshot({room(), raised}, fixedWedge());
  auto borders = std::count_if(
      withSteps.getWalls().begin(), withSteps.getWalls().end(), [](auto const& wall) {
        return wall.visible && wall.kind == ArrangementWallKind::Border;
      });
  require(withSteps.getDetail().getWedgeCount() == borders * 4,
          "a FloorStep or CeilingStep wall generated a Wedge");
}

void geometryNormalsUvsAndMaterialRouting() {
  auto data = snapshot({room()}, fixedWedge());
  for (auto const& triangle : data.getDetail().getTriangles()) {
    require(triangle.kind == DetailTriangleKind::WedgeFacet,
            "Wedge detail did not have its dedicated kind");
    require(triangle.source.kind == DetailSurfaceKind::CeilingOfFace ||
                triangle.source.kind == DetailSurfaceKind::FloorOfFace,
            "a Wedge did not route through its adjoining horizontal face");
    auto const& face = data.getArrangement().faces[triangle.source.index];
    require(face.solid, "a Wedge selected the empty side of a Border wall");
    require(!triangle.followsWallFacing,
            "a Wedge facet followed wall-facing mirroring");
    auto const& n = triangle.v[0].normal;
    require(near(n[0], triangle.v[1].normal[0]) &&
                near(n[1], triangle.v[1].normal[1]) &&
                near(n[2], triangle.v[1].normal[2]) &&
                near(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0f) &&
                (triangle.source.kind == DetailSurfaceKind::CeilingOfFace
                     ? n[2] < 0.0f
                     : n[2] > 0.0f),
            "a Wedge facet did not carry one outward flat normal");
    for (auto const& vertex : triangle.v) {
      require(near(vertex.uv[0], vertex.position[0] / 64.0f) &&
                  near(vertex.uv[1], vertex.position[1] / 64.0f),
              "Wedge UVs did not follow the ceiling world plane");
    }
  }
  auto dimensions = dimensionsAt(data, {0.0f, -8.0f});
  auto floorDimensions = dimensionsAt(data, {0.0f, -8.0f}, false);
  require(near(dimensions.reach, 6.0f) && near(dimensions.drop, 3.0f) &&
              near(dimensions.depth, 2.0f) &&
              near(floorDimensions.reach, dimensions.reach) &&
              near(floorDimensions.drop, dimensions.drop) &&
              near(floorDimensions.depth, dimensions.depth),
          "floor and ceiling Wedge vertices did not encode matching dimensions");

  auto const positions = wedgePositions(data);
  auto contains = [&](std::array<float, 3> const& expected) {
    return std::any_of(
        positions.begin(), positions.end(), [&](auto const& position) {
          return near(position[0], expected[0]) &&
                 near(position[1], expected[1]) &&
                 near(position[2], expected[2]);
        });
  };
  require(contains({0.0f, -6.785185f, 9.0f}) &&
              contains({0.0f, -7.392593f, 8.0f}) &&
              contains({0.0f, -6.785185f, 1.0f}) &&
              contains({0.0f, -7.392593f, 2.0f}),
          "floor and ceiling centre lines did not arch toward the wall");
}

void cornerWedgesUseTrihedralGeometryAndFloorCollision() {
  auto settings = fixedWedge(6.0f, 3.0f, 2.0f);
  settings.minimumCornerReach = 4.0f;
  settings.maximumCornerReach = 4.0f;
  settings.minimumCornerVerticalExtent = 3.0f;
  settings.maximumCornerVerticalExtent = 3.0f;
  auto data = snapshot({room()}, settings);

  auto triangleHas = [](bw::core::arr::DetailTriangle const& triangle,
                        std::array<float, 3> const& expected) {
    return std::any_of(
        triangle.v.begin(), triangle.v.end(), [&](auto const& vertex) {
          return near(vertex.position[0], expected[0]) &&
                 near(vertex.position[1], expected[1]) &&
                 near(vertex.position[2], expected[2]);
        });
  };
  bool ceilingCorner = false;
  bool floorCorner = false;
  for (auto const& triangle : data.getDetail().getTriangles()) {
    if (triangle.kind != DetailTriangleKind::WedgeFacet) continue;
    if (triangleHas(triangle, {-6.0f, -8.0f, 10.0f}) &&
        triangleHas(triangle, {-10.0f, -4.0f, 10.0f}) &&
        triangleHas(triangle, {-10.0f, -8.0f, 7.0f})) {
      ceilingCorner = triangle.source.kind ==
                          DetailSurfaceKind::CeilingOfFace &&
                      triangle.v[0].normal[2] < 0.0f;
    }
    if (triangleHas(triangle, {-6.0f, -8.0f, 0.0f}) &&
        triangleHas(triangle, {-10.0f, -4.0f, 0.0f}) &&
        triangleHas(triangle, {-10.0f, -8.0f, 3.0f})) {
      floorCorner =
          triangle.source.kind == DetailSurfaceKind::FloorOfFace &&
          triangle.v[0].normal[2] > 0.0f;
    }
  }
  require(ceilingCorner && floorCorner &&
              near(data.getFloorHeight({-8.666667f, -6.666667f}), 1.0f),
          "floor and ceiling Corner Wedges did not form the expected trihedral facets");

  auto tooLarge = settings;
  tooLarge.minimumCornerReach = 30.0f;
  tooLarge.maximumCornerReach = 30.0f;
  auto withoutCorners = snapshot({room()}, tooLarge);
  require(withoutCorners.getDetail().getWedgeCount() == 8,
          "a Corner Wedge whose minimum reach did not fit was generated");
}

void fittingAndStableIndependentStreams() {
  auto cappedSettings =
      WedgeGenerationParameters{true, 4.0f, 20.0f, 2.0f, 20.0f, 1.0f, 20.0f};
  cappedSettings.floorWedgesPerUnitDistance = 0.2f;
  cappedSettings.ceilingWedgesPerUnitDistance = 0.2f;
  auto capped = snapshot(
      {room(rectangle(-3, -2, 3, 2))}, cappedSettings);
  require(capped.getDetail().getWedgeCount() == 8,
          "a straightforward floor or ceiling fitting Wedge was skipped");
  for (auto const& wall : capped.getWalls()) {
    auto orientation = bw::core::arr::OrientArrangementWall(
        capped.getArrangement(), wall);
    auto dimensions = dimensionsAt(capped, (orientation.v0 + orientation.v1) * 0.5f);
    require(dimensions.reach <= (orientation.v1 - orientation.v0).length() + 0.01f &&
                dimensions.drop <= wall.maxZ - wall.minZ + 0.01f &&
                dimensions.depth <= 6.01f,
            "Wedge dimensions exceeded Arris, wall, or ceiling space");
  }

  require(!hasWedgeAt(
              snapshot({room()}, fixedWedge(30.0f, 3.0f, 2.0f)),
              {0.0f, -8.0f}),
          "an edge Wedge below minimum reach fit was not skipped");
  require(!hasWedgeAt(
              snapshot({room()}, fixedWedge(6.0f, 30.0f, 2.0f)),
              {0.0f, -8.0f}),
          "an edge Wedge below minimum wall-height fit was not skipped");
  require(!hasWedgeAt(
              snapshot({room()}, fixedWedge(6.0f, 3.0f, 30.0f)),
              {0.0f, -8.0f}),
          "an edge Wedge below minimum horizontal-depth fit was not skipped");

  WedgeGenerationParameters first{true, 4.0f, 8.0f, 2.0f, 4.0f, 2.0f, 4.0f};
  auto a = snapshot({room()}, first);
  auto repeated = snapshot({room()}, first);
  auto rotatedContour = rectangle();
  std::rotate(rotatedContour.begin(), rotatedContour.begin() + 2, rotatedContour.end());
  auto renumbered = snapshot({room(rotatedContour)}, first);
  auto reversedContour = rectangle();
  std::reverse(reversedContour.begin(), reversedContour.end());
  auto reversed = snapshot({room(reversedContour)}, first);
  require(wedgePositions(a) == wedgePositions(repeated) &&
              wedgePositions(a) == wedgePositions(renumbered) &&
              wedgePositions(a) == wedgePositions(reversed),
          "Wedge geometry depended on regeneration or edge ordering");

  auto base = dimensionsAt(a, {0.0f, -8.0f});
  auto changedReach = first;
  changedReach.minimumReach = 5.0f;
  changedReach.maximumReach = 9.0f;
  auto changed = dimensionsAt(snapshot({room()}, changedReach), {0.0f, -8.0f});
  require(near(base.drop, changed.drop) && near(base.depth, changed.depth),
          "reach authoring perturbed another Wedge random stream");
}

void completeFootprintsRespectHolesAndNonConvexBoundaries() {
  auto settings = WedgeGenerationParameters{
      true, 8.0f, 8.0f, 2.0f, 2.0f, 2.0f, 20.0f};
  settings.floorWedgesPerUnitDistance = 0.025f;
  settings.ceilingWedgesPerUnitDistance = 0.025f;
  auto control = snapshot(
      {room(rectangle(-20, -20, 20, 20))}, settings);
  auto controlDimensions = dimensionsAt(control, {0.0f, -20.0f});

  // The centre ray misses this offset hole. The complete footprint's right
  // edge reaches its near-left corner at depth 10, so apex-only fitting would
  // incorrectly retain the much larger control depth.
  auto withHole = snapshot(
      {region({rectangle(-20, -20, 20, 20),
               rectangle(2, -15, 5, -10)})},
      settings);
  auto holeDimensions = dimensionsAt(withHole, {0.0f, -20.0f});
  auto repeatedHole = snapshot(
      {region({rectangle(-20, -20, 20, 20),
               rectangle(2, -15, 5, -10)})},
      settings);
  auto expectedConstrainedDraw =
      2.0f + (controlDimensions.depth - 2.0f) * (8.0f / 18.0f);
  require(near(holeDimensions.depth, expectedConstrainedDraw, 0.02f) &&
              holeDimensions.depth < controlDimensions.depth &&
              near(holeDimensions.reach, controlDimensions.reach) &&
              near(holeDimensions.drop, controlDimensions.drop) &&
              wedgePositions(withHole) == wedgePositions(repeatedHole),
          "an offset ceiling hole did not constrain only projection depth");

  Contour nonConvex{
      {-20 * U, -20 * U}, {20 * U, -20 * U},
      {20 * U, -15 * U},  {2 * U, -15 * U},
      {2 * U, -10 * U},   {20 * U, -10 * U},
      {20 * U, 20 * U},   {-20 * U, 20 * U}};
  auto cutOut = snapshot({room(nonConvex)}, settings);
  auto cutOutDimensions = dimensionsAt(cutOut, {0.0f, -20.0f});
  require(near(cutOutDimensions.depth, expectedConstrainedDraw, 0.02f) &&
              near(cutOutDimensions.reach, controlDimensions.reach) &&
              near(cutOutDimensions.drop, controlDimensions.drop),
          "a non-convex ceiling cut-out was crossed by a Wedge footprint");

  auto reachSettings = WedgeGenerationParameters{
      true, 2.0f, 8.0f, 2.0f, 2.0f, 4.0f, 4.0f};
  reachSettings.floorWedgesPerUnitDistance = 0.025f;
  reachSettings.ceilingWedgesPerUnitDistance = 0.025f;
  Contour nearHole{{1500, -18 * U}, {3000, -18 * U},
                   {3000, -16 * U}, {1500, -16 * U}};
  auto reachControl = dimensionsAt(
      snapshot({room(rectangle(-20, -20, 20, 20))}, reachSettings),
      {0.0f, -20.0f});
  auto reachConstrained = dimensionsAt(
      snapshot(
          {region({rectangle(-20, -20, 20, 20), nearHole})},
          reachSettings),
      {0.0f, -20.0f});
  auto expectedReachDraw =
      2.0f + (reachControl.reach - 2.0f) * (4.0f / 6.0f);
  require(near(reachConstrained.reach, expectedReachDraw, 0.02f) &&
              near(reachConstrained.depth, reachControl.depth) &&
              near(reachConstrained.drop, reachControl.drop),
          "complete footprint fitting did not constrain only reach");

  auto tooDeep = settings;
  tooDeep.minimumProjectionDepth = 10.1f;
  require(!hasWedgeAt(
              snapshot(
                  {region({rectangle(-20, -20, 20, 20),
                           rectangle(2, -15, 5, -10)})},
                  tooDeep),
              {0.0f, -20.0f}) &&
              !hasWedgeAt(snapshot({room(nonConvex)}, tooDeep),
                          {0.0f, -20.0f}),
          "a footprint that could not fit its minimum was moved or retained");
}

void exactFitsAndIndependentOverlapAreAccepted() {
  auto exactSettings = fixedWedge(4.0f, 10.0f, 8.0f);
  exactSettings.floorWedgesPerUnitDistance = 0.125f;
  exactSettings.ceilingWedgesPerUnitDistance = 0.125f;
  auto exact = snapshot(
      {room(rectangle(-2, -4, 2, 4))}, exactSettings);
  auto exactDimensions = dimensionsAt(exact, {0.0f, -4.0f});
  require(near(exactDimensions.reach, 4.0f) &&
              near(exactDimensions.depth, 8.0f) &&
              near(exactDimensions.drop, 10.0f),
          "an exact-minimum boundary or wall-height fit was rejected");

  auto overlapping = snapshot(
      {room(rectangle(-10, -2, 10, 2))},
      fixedWedge(4.0f, 2.0f, 3.0f));
  require(overlapping.getDetail().getWedgeCount() == 4 &&
              hasWedgeAt(overlapping, {0.0f, -2.0f}) &&
              hasWedgeAt(overlapping, {0.0f, 2.0f}) &&
              near(dimensionsAt(overlapping, {0.0f, -2.0f}).depth, 3.0f) &&
              near(dimensionsAt(overlapping, {0.0f, 2.0f}).depth, 3.0f) &&
              overlapping.getFloorHeight({0.0f, 0.0f}) > 0.0f,
          "opposite overlapping Wedges were resolved against one another");
}

bw::core::ChipGenerationParameters fixedArrisChip(
    float depth, float reach) {
  bw::core::ChipGenerationParameters result;
  result.minimumDepth = depth;
  result.maximumDepth = depth;
  result.minimumReach = reach;
  result.maximumReach = reach;
  result.minimumSpacing = 256.0f;
  result.probability = 1.0f;
  return result;
}

ArrangementPrimitive raisedCeiling(Contour contour) {
  auto result = room(std::move(contour));
  result.priority = 2;
  result.primitiveIndex = 1;
  result.properties.ceilingZ = 20.0f;
  return result;
}

ArrangementPrimitive loweredFloor(Contour contour) {
  auto result = room(std::move(contour));
  result.priority = 2;
  result.primitiveIndex = 1;
  result.properties.floorZ = -10.0f;
  return result;
}

Contour reentrantRoom() {
  return {{-6 * U, -6 * U}, {0, -6 * U}, {0, 0},
          {6 * U, 0},       {6 * U, 6 * U}, {-6 * U, 6 * U}};
}

void horizontalChipFootprintsConstrainWedges() {
  auto settings = WedgeGenerationParameters{
      true, 4.0f, 4.0f, 2.0f, 2.0f, 1.0f, 8.0f};
  auto control = snapshot({room()}, settings);
  auto chipped = snapshot(
      {room(), raisedCeiling(rectangle(-2, -3, 2, 3))}, settings,
      fixedArrisChip(2.0f, 4.0f));
  auto controlDimensions = dimensionsAt(control, {0.0f, -8.0f});
  auto chippedDimensions = dimensionsAt(chipped, {0.0f, -8.0f});
  require(chipped.getDetail().getChipCount() > 0 &&
              chippedDimensions.depth < controlDimensions.depth &&
              chippedDimensions.depth <= 3.01f &&
              near(chippedDimensions.reach, controlDimensions.reach) &&
              near(chippedDimensions.drop, controlDimensions.drop),
          "a Horizontal Chip ceiling footprint did not constrain only Wedge projection");

  auto rejectedSettings = settings;
  rejectedSettings.minimumProjectionDepth = 3.1f;
  require(!hasWedgeAt(
              snapshot(
                  {room(), raisedCeiling(rectangle(-2, -3, 2, 3))},
                  rejectedSettings, fixedArrisChip(2.0f, 4.0f)),
              {0.0f, -8.0f}),
          "a Wedge refilled a Horizontal Chip when its minimum could not fit");

  auto reachSettings = WedgeGenerationParameters{
      true, 1.0f, 8.0f, 2.0f, 2.0f, 4.0f, 4.0f};
  auto reachControl = dimensionsAt(
      snapshot(
          {room(), raisedCeiling(rectangle(2, -6, 4, -2))},
          reachSettings),
      {0.0f, -8.0f});
  auto prismatic = fixedArrisChip(1.0f, 2.0f);
  prismatic.types = {bw::core::ChipType::PrismaticNotch};
  auto reachChipped = dimensionsAt(
      snapshot(
          {room(), raisedCeiling(rectangle(2, -6, 4, -2))},
          reachSettings, prismatic),
      {0.0f, -8.0f});
  auto reachRandom = (reachControl.reach - 1.0f) / 7.0f;
  auto fittedReach = 1.0f + (reachChipped.reach - 1.0f) / reachRandom;
  require(reachChipped.reach < reachControl.reach &&
              near(reachChipped.drop, reachControl.drop) &&
              near(reachChipped.depth, reachControl.depth),
          "a Horizontal Chip top-Arris reservation did not constrain only Wedge reach");
  reachSettings.minimumReach = fittedReach + 0.1f;
  require(!hasWedgeAt(
              snapshot(
                  {room(), raisedCeiling(rectangle(2, -6, 4, -2))},
                  reachSettings, prismatic),
              {0.0f, -8.0f}),
          "a Wedge covered a Horizontal Chip below minimum reach");

  auto floorControl = snapshot({room()}, settings);
  auto floorChipped = snapshot(
      {room(), loweredFloor(rectangle(-2, -3, 2, 3))}, settings,
      fixedArrisChip(2.0f, 4.0f));
  auto floorControlDimensions =
      dimensionsAt(floorControl, {0.0f, -8.0f}, false);
  auto floorChippedDimensions =
      dimensionsAt(floorChipped, {0.0f, -8.0f}, false);
  require(floorChippedDimensions.depth < floorControlDimensions.depth &&
              floorChippedDimensions.depth <= 3.01f &&
              near(floorChippedDimensions.reach,
                   floorControlDimensions.reach) &&
              near(floorChippedDimensions.drop,
                   floorControlDimensions.drop),
          "a Horizontal Chip floor footprint did not constrain the floor Wedge");

  rejectedSettings = settings;
  rejectedSettings.minimumProjectionDepth = 3.1f;
  require(!hasWedgeAt(
              snapshot(
                  {room(), loweredFloor(rectangle(-2, -3, 2, 3))},
                  rejectedSettings, fixedArrisChip(2.0f, 4.0f)),
              {0.0f, -8.0f}, false),
          "a floor Wedge refilled a Horizontal Chip below its minimum");
}

bw::core::ChipGenerationParameters fixedCornerChip(float distance) {
  bw::core::ChipGenerationParameters result;
  result.minimumCornerDistance = distance;
  result.maximumCornerDistance = distance;
  result.cornerProbability = 1.0f;
  return result;
}

std::vector<ArrangementPrimitive> roomWithLoweredCeilingCorner() {
  auto outer = room(rectangle(-4, -4, 4, 4));
  auto lowered = room(rectangle(-4, -4, 0, 0));
  lowered.priority = 2;
  lowered.primitiveIndex = 1;
  lowered.properties.ceilingZ = 5.0f;
  return {outer, lowered};
}

void cornerChipReservationsConstrainWedges() {
  auto settings = WedgeGenerationParameters{
      true, 4.0f, 4.0f, 2.0f, 2.0f, 1.0f, 4.0f};
  settings.floorWedgesPerUnitDistance = 0.25f;
  settings.ceilingWedgesPerUnitDistance = 0.25f;
  auto control = snapshot(roomWithLoweredCeilingCorner(), settings);
  auto chipped = snapshot(
      roomWithLoweredCeilingCorner(), settings, fixedCornerChip(3.0f));
  auto controlDimensions = dimensionsAt(control, {-2.0f, -4.0f});
  auto chippedDimensions = dimensionsAt(chipped, {-2.0f, -4.0f});
  require(chipped.getDetail().getChipCount() == 1 &&
              chippedDimensions.depth < controlDimensions.depth &&
              chippedDimensions.depth <= 3.01f &&
              near(chippedDimensions.reach, controlDimensions.reach) &&
              near(chippedDimensions.drop, controlDimensions.drop),
          "a Corner Chip ceiling reservation did not constrain only Wedge projection");

  auto rejectedSettings = settings;
  rejectedSettings.minimumProjectionDepth = 3.1f;
  require(!hasWedgeAt(
              snapshot(
                  roomWithLoweredCeilingCorner(), rejectedSettings,
                  fixedCornerChip(3.0f)),
              {-2.0f, -4.0f}),
          "a Wedge refilled a Corner Chip when its minimum could not fit");
}

void verticalChipWallNotchesConstrainWedges() {
  auto settings = WedgeGenerationParameters{
      true, 4.0f, 4.0f, 1.0f, 8.0f, 1.0f, 1.0f};
  settings.floorWedgesPerUnitDistance = 0.2f;
  settings.ceilingWedgesPerUnitDistance = 0.2f;
  auto control = snapshot({room(reentrantRoom())}, settings);
  auto chip = fixedArrisChip(4.0f, 4.0f);
  chip.types = {bw::core::ChipType::PrismaticNotch};
  auto chipped = snapshot({room(reentrantRoom())}, settings, chip);
  auto controlDimensions = dimensionsAt(control, {0.0f, -3.0f});
  auto chippedDimensions = dimensionsAt(chipped, {0.0f, -3.0f});
  require(chipped.getDetail().getChipCount() == 1 &&
              chippedDimensions.drop < controlDimensions.drop &&
              chippedDimensions.drop <= 3.01f &&
              near(chippedDimensions.reach, controlDimensions.reach) &&
              near(chippedDimensions.depth, controlDimensions.depth),
          "a Vertical Chip wall notch did not constrain only Wedge drop-down height");

  auto rejectedSettings = settings;
  rejectedSettings.minimumDropDownHeight = 3.1f;
  require(!hasWedgeAt(
              snapshot({room(reentrantRoom())}, rejectedSettings, chip),
              {0.0f, -3.0f}),
          "a Wedge closed a Vertical Chip when its minimum could not fit");

}

void wedgeGenerationDoesNotPerturbChips() {
  auto primitives = std::vector<ArrangementPrimitive>{
      room(), raisedCeiling(rectangle(-2, -3, 2, 3))};
  auto chips = fixedArrisChip(2.0f, 4.0f);
  auto withoutWedges = snapshot(primitives, {}, chips);
  auto withWedges = snapshot(
      primitives, {true, 2.0f, 6.0f, 1.0f, 4.0f, 1.0f, 8.0f}, chips);
  require(withWedges.getDetail().getChipCount() ==
                  withoutWedges.getDetail().getChipCount() &&
              withWedges.getDetail().getSuppressed() ==
                  withoutWedges.getDetail().getSuppressed() &&
              chipAndRemainderPositions(withWedges) ==
                  chipAndRemainderPositions(withoutWedges),
          "Wedge fitting changed existing Chip geometry, counts, or suppression");
}

void qualityRecursivelyTessellatesWithDeterministicVariation() {
  auto baseSettings = fixedWedge();
  baseSettings.quality = 0;
  auto base = snapshot({room()}, baseSettings);
  auto countFacets = [](ArrangementWorldData const& data) {
    return std::count_if(
        data.getDetail().getTriangles().begin(),
        data.getDetail().getTriangles().end(), [](auto const& triangle) {
          return triangle.kind == DetailTriangleKind::WedgeFacet;
        });
  };

  auto qualityOneSettings = baseSettings;
  qualityOneSettings.quality = 1;
  auto qualityOne = snapshot({room()}, qualityOneSettings);
  auto repeatedQualityOne = snapshot({room()}, qualityOneSettings);
  auto qualityTwoSettings = baseSettings;
  qualityTwoSettings.quality = 2;
  auto qualityTwo = snapshot({room()}, qualityTwoSettings);
  auto baseFacetCount = countFacets(base);
  require(countFacets(qualityOne) == baseFacetCount * 3 &&
              countFacets(qualityTwo) == baseFacetCount * 9 &&
              qualityOne.getDetail().getWedgeCount() ==
                  base.getDetail().getWedgeCount() &&
              wedgePositions(qualityOne) == wedgePositions(repeatedQualityOne) &&
              qualityOne.getFloorHeight({0.0f, -6.785185f}) > 0.0f,
          "Wedge quality did not recursively and deterministically subdivide each facet");

  bool foundDisplacedCentroid = false;
  for (auto const& triangle : base.getDetail().getTriangles()) {
    if (triangle.kind != DetailTriangleKind::WedgeFacet) continue;
    std::array<float, 3> centroid{};
    for (auto const& vertex : triangle.v) {
      for (size_t axis = 0; axis < 3; ++axis) {
        centroid[axis] += vertex.position[axis] / 3.0f;
      }
    }
    auto distance = [&](std::array<float, 3> const& position) {
      auto sum = 0.0f;
      for (size_t axis = 0; axis < 3; ++axis) {
        auto delta = position[axis] - centroid[axis];
        sum += delta * delta;
      }
      return std::sqrt(sum);
    };
    auto nearest = std::numeric_limits<float>::max();
    for (auto const& subdivided : qualityOne.getDetail().getTriangles()) {
      if (subdivided.kind != DetailTriangleKind::WedgeFacet ||
          subdivided.source != triangle.source) {
        continue;
      }
      for (auto const& vertex : subdivided.v) {
        nearest = std::min(nearest, distance(vertex.position));
      }
    }
    if (nearest > 0.0001f) {
      foundDisplacedCentroid = true;
      break;
    }
  }
  require(foundDisplacedCentroid,
          "tessellation centre points were not displaced from their source facets");
}

void worldFrequencyControlsEdgeAndCornerAttempts() {
  auto settings = fixedWedge();
  settings.floorWedgesPerUnitDistance = 0.0f;
  settings.ceilingWedgesPerUnitDistance = 0.1f;
  settings.cornerWedgeProbability = 0.0f;
  auto ceilingOnly = snapshot({room()}, settings);
  require(ceilingOnly.getDetail().getWedgeCount() == 8 &&
              std::all_of(
                  ceilingOnly.getDetail().getTriangles().begin(),
                  ceilingOnly.getDetail().getTriangles().end(),
                  [](auto const& triangle) {
                    return triangle.kind != DetailTriangleKind::WedgeFacet ||
                           triangle.source.kind ==
                               DetailSurfaceKind::CeilingOfFace;
                  }),
          "ceiling Wedge density did not control attempts per wall length");

  settings.floorWedgesPerUnitDistance = 0.05f;
  settings.ceilingWedgesPerUnitDistance = 0.0f;
  auto floorOnly = snapshot({room()}, settings);
  require(floorOnly.getDetail().getWedgeCount() == 4 &&
              std::all_of(
                  floorOnly.getDetail().getTriangles().begin(),
                  floorOnly.getDetail().getTriangles().end(),
                  [](auto const& triangle) {
                    return triangle.kind != DetailTriangleKind::WedgeFacet ||
                           triangle.source.kind == DetailSurfaceKind::FloorOfFace;
                  }),
          "floor Wedge density did not independently control attempts");

  settings.floorWedgesPerUnitDistance = 0.0f;
  settings.cornerWedgeProbability = 1.0f;
  auto allCorners = snapshot({room()}, settings);
  require(allCorners.getDetail().getWedgeCount() == 8,
          "a probability of one did not create every floor and ceiling Corner Wedge");
  settings.cornerWedgeProbability = 0.0f;
  require(snapshot({room()}, settings).getDetail().getWedgeCount() == 0,
          "a probability of zero created a Corner Wedge");
}

void floorWedgesJoinCollisionWithoutChangingOtherQueries() {
  bw::core::ArrangementStats stats;
  auto arrangement = bw::core::arr::BuildArrangement({room()});
  ArrangementWorldData enabled(
      arrangement, wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}),
      16.0f, 8.0f, &stats, fixedWedge());
  ArrangementWorldData disabled(
      arrangement, wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}),
      16.0f, 8.0f);
  require(stats.wedgeCount == 16 && stats.chipCount == 0,
          "Arrangement diagnostics did not separate Wedges from Chips");
  require(enabled.getTriangles().size() == disabled.getTriangles().size() &&
              enabled.getWalls().size() == disabled.getWalls().size() &&
              enabled.getContainingFaceIndex({0.0f, 0.0f}) ==
                  disabled.getContainingFaceIndex({0.0f, 0.0f}) &&
              near(enabled.getFloorHeight({0.0f, 0.0f}),
                   disabled.getFloorHeight({0.0f, 0.0f})) &&
              near(enabled.getFloorHeight({0.0f, -6.785185f}), 1.0f) &&
              near(disabled.getFloorHeight({0.0f, -6.785185f}), 0.0f) &&
              near(enabled.getCeilingHeight({0.0f, -6.785185f}),
                   disabled.getCeilingHeight({0.0f, -6.785185f})) &&
              near(enabled.getCeilingHeight({0.0f, 0.0f}),
                   disabled.getCeilingHeight({0.0f, 0.0f})) &&
              enabled.getWallsNear({9.5f, 0.0f}, 1.0f) ==
                  disabled.getWallsNear({9.5f, 0.0f}, 1.0f),
          "floor Wedge collision or an otherwise unchanged query was incorrect");
}
}  // namespace

int main() {
  try {
    disabledAndEligibilityContract();
    geometryNormalsUvsAndMaterialRouting();
    cornerWedgesUseTrihedralGeometryAndFloorCollision();
    fittingAndStableIndependentStreams();
    completeFootprintsRespectHolesAndNonConvexBoundaries();
    exactFitsAndIndependentOverlapAreAccepted();
    horizontalChipFootprintsConstrainWedges();
    cornerChipReservationsConstrainWedges();
    verticalChipWallNotchesConstrainWedges();
    wedgeGenerationDoesNotPerturbChips();
    qualityRecursivelyTessellatesWithDeterministicVariation();
    worldFrequencyControlsEdgeAndCornerAttempts();
    floorWedgesJoinCollisionWithoutChangingOtherQueries();
    std::cout << "Wedges are deterministic additive Border-wall detail\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
