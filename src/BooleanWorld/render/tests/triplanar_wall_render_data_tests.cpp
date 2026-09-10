#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/SurfaceMaterialReference.h>

#include "TriplanarWallRenderData.h"

namespace {
using bw::core::arr::ArrangementEdge;
using bw::core::arr::ArrangementResult;
using bw::core::arr::ArrangementWall;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::ToFixedPointCoordinate;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float epsilon = 0.0001f) {
  return std::abs(left - right) <= epsilon;
}

bool near(wp::Vector2 const& left, wp::Vector2 const& right) {
  return near(left.x, right.x) && near(left.y, right.y);
}

uint32_t addVertex(ArrangementResult& arrangement, float x, float y) {
  arrangement.vertices.push_back(
      FixedPointVertex{ToFixedPointCoordinate(x), ToFixedPointCoordinate(y)});
  return static_cast<uint32_t>(arrangement.vertices.size() - 1);
}

uint32_t addEdge(
    ArrangementResult& arrangement, uint32_t first, uint32_t second) {
  ArrangementEdge edge{};
  edge.v[0] = first;
  edge.v[1] = second;
  edge.face[0] = 0;
  edge.face[1] = 1;
  arrangement.edges.push_back(edge);
  return static_cast<uint32_t>(arrangement.edges.size() - 1);
}

ArrangementWall wall(
    uint32_t edge, uint16_t palette, float lower, float upper) {
  ArrangementWall result{};
  result.edge = edge;
  result.minZ = lower;
  result.maxZ = upper;
  result.paletteIndex = palette;
  result.kind = ArrangementWallKind::Border;
  result.clearance = upper - lower;
  result.visible = true;
  result.bottomZ = {lower, lower};
  result.topZ = {upper, upper};
  result.sourceEdgeParameter = {0.0f, 1.0f};
  result.frontFace = 0;
  result.ownerFace = 0;
  return result;
}

struct Fixture {
  ArrangementResult arrangement;
  std::vector<ArrangementWall> walls;
};

Fixture buildFixture() {
  Fixture fixture;
  fixture.arrangement.faces.resize(2);
  fixture.arrangement.palette.resize(3);
  fixture.arrangement.palette[1].wallMaterialId =
      bw::core::SurfaceMaterialReference::triplanar("World/AsymmetricA");
  fixture.arrangement.palette[2].wallMaterialId =
      bw::core::SurfaceMaterialReference::triplanar("World/AsymmetricB");

  auto junction = addVertex(fixture.arrangement, 0.0f, 0.0f);
  auto east = addVertex(fixture.arrangement, 12.0f, 0.0f);
  auto angled = addVertex(fixture.arrangement, 7.0f, 11.0f);
  auto westNorth = addVertex(fixture.arrangement, -9.0f, 4.0f);
  auto south = addVertex(fixture.arrangement, 0.0f, -12.0f);
  // The same fixed-point position deliberately has a different Arrangement
  // vertex identity and therefore is not connected to junction.
  auto disconnected = addVertex(fixture.arrangement, 0.0f, 0.0f);
  auto disconnectedEnd = addVertex(fixture.arrangement, 8.0f, 8.0f);

  fixture.walls.push_back(
      wall(addEdge(fixture.arrangement, junction, east), 1, 0.0f, 20.0f));
  fixture.walls.push_back(
      wall(addEdge(fixture.arrangement, junction, angled), 1, 5.0f, 15.0f));
  fixture.walls.push_back(wall(
      addEdge(fixture.arrangement, junction, westNorth), 1, 10.0f, 20.0f));
  fixture.walls.push_back(
      wall(addEdge(fixture.arrangement, junction, south), 2, 0.0f, 20.0f));
  fixture.walls.push_back(wall(
      addEdge(fixture.arrangement, disconnected, disconnectedEnd), 1, 0.0f,
      20.0f));

  auto cancellationJunction = addVertex(fixture.arrangement, 30.0f, 30.0f);
  auto cancellationEast = addVertex(fixture.arrangement, 40.0f, 30.0f);
  auto cancellationWest = addVertex(fixture.arrangement, 20.0f, 30.0f);
  fixture.walls.push_back(wall(
      addEdge(fixture.arrangement, cancellationJunction, cancellationEast), 1,
      0.0f, 8.0f));
  fixture.walls.push_back(wall(
      addEdge(fixture.arrangement, cancellationJunction, cancellationWest), 1,
      0.0f, 8.0f));
  return fixture;
}

void connectedProfilesUseExactElevationGroups() {
  auto fixture = buildFixture();
  auto data = BuildTriplanarWallProjectionData(
      fixture.arrangement, fixture.walls);
  require(data.size() == fixture.walls.size(),
          "Projection data is not parallel to Arrangement walls");

  auto const& tall = data[0].endpoints[0].spans;
  require(tall.size() == 4 && tall[0].lowerElevation == 0.0f &&
              tall[0].upperElevation == 5.0f &&
              tall[1].upperElevation == 10.0f &&
              tall[2].upperElevation == 15.0f &&
              tall[3].upperElevation == 20.0f,
          "unequal walls did not produce exact elementary elevation spans");
  auto const& middle = data[1].endpoints[0].spans;
  auto const& upper = data[2].endpoints[0].spans;
  require(middle.size() == 2 && upper.size() == 2 &&
              near(tall[1].projectionNormal, middle[0].projectionNormal) &&
              near(tall[2].projectionNormal, middle[1].projectionNormal) &&
              near(tall[2].projectionNormal, upper[0].projectionNormal) &&
              near(tall[3].projectionNormal, upper[1].projectionNormal),
          "incident arbitrary-angle walls did not share Projection normals");

  auto tallGeometric = bw::core::arr::OrientArrangementWall(
                           fixture.arrangement, fixture.walls[0])
                           .normal;
  require(near(tall.front().projectionNormal, tallGeometric),
          "unmatched lower wall span inherited an absent wall orientation");

  auto differentMaterial = data[3].endpoints[0].spans;
  auto differentGeometric = bw::core::arr::OrientArrangementWall(
                                fixture.arrangement, fixture.walls[3])
                                .normal;
  require(differentMaterial.size() == 1 &&
              near(differentMaterial[0].projectionNormal, differentGeometric),
          "a different Triplanar resource joined the continuity group");

  auto disconnectedProfile = data[4].endpoints[0].spans;
  auto disconnectedGeometric = bw::core::arr::OrientArrangementWall(
                                   fixture.arrangement, fixture.walls[4])
                                   .normal;
  require(disconnectedProfile.size() == 1 &&
              near(disconnectedProfile[0].projectionNormal,
                   disconnectedGeometric),
          "Arrangement-disconnected coincident walls influenced each other");

  for (auto wallIndex : {size_t{5}, size_t{6}}) {
    auto const& cancellation = data[wallIndex].endpoints[0].spans;
    require(cancellation.size() == 1 &&
                near(cancellation[0].projectionNormal, {1.0f, 0.0f}),
            "cancelling incident normals did not use fixed World +X fallback");
  }
}

void renderOnlyTrianglesSplitTheTallWall() {
  auto fixture = buildFixture();
  auto data = BuildTriplanarWallProjectionData(
      fixture.arrangement, fixture.walls);
  auto triangles = BuildTriplanarWallRenderTriangles(
      fixture.arrangement, fixture.walls[0], data[0]);
  require(triangles.size() == 8,
          "four elevation spans did not produce four render-only quads");

  std::vector<float> junctionElevations;
  double area = 0.0;
  for (auto const& triangle : triangles) {
    auto const& a = triangle.vertices[0];
    auto const& b = triangle.vertices[1];
    auto const& c = triangle.vertices[2];
    auto cross = (b.position.x - a.position.x) * (c.elevation - a.elevation) -
                 (b.elevation - a.elevation) *
                     (c.position.x - a.position.x);
    area += std::abs(cross) * 0.5;
    for (auto const& vertex : triangle.vertices) {
      require(near(vertex.position.y, 0.0f) &&
                  vertex.elevation >= 0.0f && vertex.elevation <= 20.0f,
              "render split changed the wall plane or authored extent");
      if (near(vertex.position.x, 0.0f)) {
        junctionElevations.push_back(vertex.elevation);
      }
    }
  }
  for (float boundary : {0.0f, 5.0f, 10.0f, 15.0f, 20.0f}) {
    require(std::ranges::find(junctionElevations, boundary) !=
                junctionElevations.end(),
            "render triangles omitted an exact incident-wall boundary");
  }
  require(std::abs(area - 240.0) < 0.001,
          "render-only splitting changed wall surface area");

  auto middleTriangles = BuildTriplanarWallRenderTriangles(
      fixture.arrangement, fixture.walls[1], data[1]);
  bool comparedSharedSpan = false;
  for (auto const& tallTriangle : triangles) {
    auto tallAverage = (tallTriangle.vertices[0].elevation +
                        tallTriangle.vertices[1].elevation +
                        tallTriangle.vertices[2].elevation) /
                       3.0f;
    if (tallAverage <= 5.0f || tallAverage >= 10.0f) continue;
    for (auto const& middleTriangle : middleTriangles) {
      auto middleAverage = (middleTriangle.vertices[0].elevation +
                            middleTriangle.vertices[1].elevation +
                            middleTriangle.vertices[2].elevation) /
                           3.0f;
      if (middleAverage <= 5.0f || middleAverage >= 10.0f) continue;
      for (auto const& tallVertex : tallTriangle.vertices) {
        if (!near(tallVertex.position, {0.0f, 0.0f})) continue;
        for (auto const& middleVertex : middleTriangle.vertices) {
          if (tallVertex.elevation == middleVertex.elevation &&
              near(middleVertex.position, {0.0f, 0.0f})) {
            require(near(tallVertex.projectionNormal,
                         middleVertex.projectionNormal),
                    "emitted common-edge vertices do not share Projection normals");
            comparedSharedSpan = true;
          }
        }
      }
    }
  }
  require(comparedSharedSpan,
          "render-data test found no exact common-edge vertex to compare");
}

std::string readText(char const* path) {
  std::ifstream input(path);
  return {(std::istreambuf_iterator<char>(input)), {}};
}

void shadersKeepProjectionIndependentFromLighting() {
  auto vertex = readText(BW_WORLD_VERTEX_SHADER);
  auto fragment3d = readText(BW_WORLD_PBR_SHADER);
  auto fragment2d = readText(BW_WORLD_PBR_2D_SHADER);
  require(vertex.find("@Out(vec3 FRAGNORMAL)") != std::string::npos &&
              vertex.find("@Out(vec3 PROJECTION_NORMAL)") !=
                  std::string::npos &&
              vertex.find("@In(NORMAL)") != std::string::npos &&
              vertex.find("surfaceData.xyz") != std::string::npos,
          "geometric and Projection normals do not use independent channels");
  for (auto const* fragment : {&fragment3d, &fragment2d}) {
    require(fragment->find("normalize(@In(PROJECTION_NORMAL))") !=
                    std::string::npos &&
                fragment->find("triplanarAlbedo") != std::string::npos,
            "Triplanar projection does not consume the Projection normal");
  }
}
}  // namespace

int main() {
  try {
    connectedProfilesUseExactElevationGroups();
    renderOnlyTrianglesSplitTheTallWall();
    shadersKeepProjectionIndependentFromLighting();
    std::cout << "Triplanar wall render-data tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
