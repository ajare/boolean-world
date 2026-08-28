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

ArrangementPrimitive room(
    Contour contour = rectangle(),
    std::vector<std::optional<bool>> visibility = {}) {
  bw::core::PrimitivePropertySet properties;
  properties.floorZ = 0.0f;
  properties.ceilingZ = 10.0f;
  properties.ceilingMaterialId = "ceiling.fixture";
  return {{std::move(contour)}, Primitive::Operation::Union,
          Primitive::FillRule::EvenOdd, 1, 0, properties, {},
          visibility.empty()
              ? std::vector<std::vector<std::optional<bool>>>{}
              : std::vector<std::vector<std::optional<bool>>>{visibility}};
}

WedgeGenerationParameters fixedWedge(
    float reach = 6.0f, float drop = 3.0f, float depth = 2.0f) {
  return {true, reach, reach, drop, drop, depth, depth};
}

ArrangementWorldData snapshot(
    std::vector<ArrangementPrimitive> primitives,
    WedgeGenerationParameters const& settings = {}) {
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

struct Dimensions {
  float reach{};
  float drop{};
  float depth{};
};

Dimensions dimensionsAt(
    ArrangementWorldData const& data, wp::Vector2 const& midpoint) {
  std::set<std::array<float, 3>> positions;
  for (auto const& triangle : data.getDetail().getTriangles()) {
    if (triangle.kind != DetailTriangleKind::WedgeFacet) continue;
    bool belongs = false;
    for (auto const& vertex : triangle.v) {
      belongs |= near(vertex.position[0], midpoint.x) &&
                 near(vertex.position[1], midpoint.y) &&
                 vertex.position[2] < 9.99f;
    }
    if (belongs) {
      for (auto const& vertex : triangle.v) positions.insert(vertex.position);
    }
  }
  require(positions.size() == 4, "a Wedge did not expose four vertices");
  std::vector<std::array<float, 3>> endpoints;
  std::array<float, 3> projected{};
  std::array<float, 3> dropped{};
  std::map<std::array<float, 3>, int> occurrences;
  for (auto const& triangle : data.getDetail().getTriangles()) {
    bool belongs = false;
    for (auto const& vertex : triangle.v) {
      belongs |= near(vertex.position[0], midpoint.x) &&
                 near(vertex.position[1], midpoint.y) &&
                 vertex.position[2] < 9.99f;
    }
    if (belongs) {
      for (auto const& vertex : triangle.v) ++occurrences[vertex.position];
    }
  }
  for (auto const& [position, count] : occurrences) {
    if (!near(position[2], 10.0f)) dropped = position;
    else if (count == 2) projected = position;
    else endpoints.push_back(position);
  }
  require(endpoints.size() == 2,
          "a Wedge did not have two distinct reach endpoints");
  auto endpointDistance = std::hypot(
      endpoints[0][0] - endpoints[1][0],
      endpoints[0][1] - endpoints[1][1]);
  return {endpointDistance, 10.0f - dropped[2],
          std::hypot(projected[0] - midpoint.x, projected[1] - midpoint.y)};
}

void disabledAndEligibilityContract() {
  auto disabled = snapshot({room()});
  require(disabled.getDetail().getWedgeCount() == 0 &&
              disabled.getDetail().getTriangles().empty(),
          "disabled Wedge settings generated detail");

  auto enabled = snapshot({room()}, fixedWedge());
  require(enabled.getDetail().getWedgeCount() == 4,
          "one Wedge was not generated for every visible Border wall");
  require(enabled.getDetail().getTriangles().size() == 8,
          "a Wedge did not emit exactly two exposed facets");
  require(enabled.getDetail().getSuppressed().empty(),
          "additive Wedges suppressed an attachment surface");

  auto hidden = snapshot(
      {room(rectangle(), {false, true, true, true})}, fixedWedge());
  require(hidden.getDetail().getWedgeCount() == 3,
          "an invisible Border wall generated a Wedge");

  auto raised = room(rectangle(-4, -3, 4, 3));
  raised.priority = 2;
  raised.primitiveIndex = 1;
  raised.properties.floorZ = 4.0f;
  auto withSteps = snapshot({room(), raised}, fixedWedge());
  auto borders = std::count_if(
      withSteps.getWalls().begin(), withSteps.getWalls().end(), [](auto const& wall) {
        return wall.visible && wall.kind == ArrangementWallKind::Border;
      });
  require(withSteps.getDetail().getWedgeCount() == borders,
          "a FloorStep or CeilingStep wall generated a Wedge");
}

void geometryNormalsUvsAndMaterialRouting() {
  auto data = snapshot({room()}, fixedWedge());
  for (auto const& triangle : data.getDetail().getTriangles()) {
    require(triangle.kind == DetailTriangleKind::WedgeFacet,
            "Wedge detail did not have its dedicated kind");
    require(triangle.source.kind == DetailSurfaceKind::CeilingOfFace,
            "a Wedge did not route through its adjoining ceiling face");
    auto const& face = data.getArrangement().faces[triangle.source.index];
    require(face.solid, "a Wedge selected the empty side of a Border wall");
    require(!triangle.followsWallFacing,
            "a Wedge facet followed wall-facing mirroring");
    auto const& n = triangle.v[0].normal;
    require(near(n[0], triangle.v[1].normal[0]) &&
                near(n[1], triangle.v[1].normal[1]) &&
                near(n[2], triangle.v[1].normal[2]) &&
                near(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 1.0f) &&
                n[2] < 0.0f,
            "a Wedge facet did not carry one outward flat normal");
    for (auto const& vertex : triangle.v) {
      require(near(vertex.uv[0], vertex.position[0] / 64.0f) &&
                  near(vertex.uv[1], vertex.position[1] / 64.0f),
              "Wedge UVs did not follow the ceiling world plane");
    }
  }
  auto dimensions = dimensionsAt(data, {0.0f, -8.0f});
  require(near(dimensions.reach, 6.0f) && near(dimensions.drop, 3.0f) &&
              near(dimensions.depth, 2.0f),
          "Wedge vertices did not encode reach, projection, and drop");
}

void fittingAndStableIndependentStreams() {
  auto capped = snapshot(
      {room(rectangle(-3, -2, 3, 2))},
      {true, 4.0f, 20.0f, 2.0f, 20.0f, 1.0f, 20.0f});
  require(capped.getDetail().getWedgeCount() == 4,
          "a straightforward fitting Wedge was skipped");
  for (auto const& wall : capped.getWalls()) {
    auto orientation = bw::core::arr::OrientArrangementWall(
        capped.getArrangement(), wall);
    auto dimensions = dimensionsAt(capped, (orientation.v0 + orientation.v1) * 0.5f);
    require(dimensions.reach <= (orientation.v1 - orientation.v0).length() + 0.01f &&
                dimensions.drop <= wall.maxZ - wall.minZ + 0.01f &&
                dimensions.depth <= 6.01f,
            "Wedge dimensions exceeded Arris, wall, or ceiling space");
  }

  require(snapshot({room()}, fixedWedge(30.0f, 3.0f, 2.0f))
              .getDetail().getWedgeCount() == 0,
          "a Wedge below minimum reach fit was not skipped");
  require(snapshot({room()}, fixedWedge(6.0f, 30.0f, 2.0f))
              .getDetail().getWedgeCount() == 0,
          "a Wedge below minimum wall-height fit was not skipped");
  require(snapshot({room()}, fixedWedge(6.0f, 3.0f, 30.0f))
              .getDetail().getWedgeCount() == 0,
          "a Wedge below minimum ceiling-depth fit was not skipped");

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

void gameplayQueriesAndDiagnosticsRemainSeparate() {
  bw::core::ArrangementStats stats;
  auto arrangement = bw::core::arr::BuildArrangement({room()});
  ArrangementWorldData enabled(
      arrangement, wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}),
      16.0f, 8.0f, &stats, fixedWedge());
  ArrangementWorldData disabled(
      arrangement, wp::BoundingBox({-64.0f, -64.0f}, {128.0f, 128.0f}),
      16.0f, 8.0f);
  require(stats.wedgeCount == 4 && stats.chipCount == 0,
          "Arrangement diagnostics did not separate Wedges from Chips");
  require(enabled.getTriangles().size() == disabled.getTriangles().size() &&
              enabled.getWalls().size() == disabled.getWalls().size() &&
              enabled.getContainingFaceIndex({0.0f, 0.0f}) ==
                  disabled.getContainingFaceIndex({0.0f, 0.0f}) &&
              near(enabled.getFloorHeight({0.0f, 0.0f}),
                   disabled.getFloorHeight({0.0f, 0.0f})) &&
              near(enabled.getCeilingHeight({0.0f, 0.0f}),
                   disabled.getCeilingHeight({0.0f, 0.0f})) &&
              enabled.getWallsNear({9.5f, 0.0f}, 1.0f) ==
                  disabled.getWallsNear({9.5f, 0.0f}, 1.0f),
          "visual-only Wedges changed an Arrangement gameplay query");
}
}  // namespace

int main() {
  try {
    disabledAndEligibilityContract();
    geometryNormalsUvsAndMaterialRouting();
    fittingAndStableIndependentStreams();
    gameplayQueriesAndDiagnosticsRemainSeparate();
    std::cout << "Wedges are deterministic additive Border-wall detail\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
