#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/CoreException.h>
#include <core/Elevation.h>

namespace {
using bw::core::Elevation;
using bw::core::Primitive;
using bw::core::PrimitivePropertySet;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementWallKind;
using bw::core::arr::Contour;

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon, message);
}

Contour rectangle(
    int64_t left, int64_t bottom, int64_t right, int64_t top) {
  return {{left, bottom}, {right, bottom}, {right, top}, {left, top}};
}

ArrangementPrimitive primitive(
    Contour contour, uint64_t priority, PrimitivePropertySet properties) {
  return {{std::move(contour)},
          Primitive::Operation::Union,
          Primitive::FillRule::NonZero,
          priority,
          uint32_t(priority),
          std::move(properties)};
}

wp::Vector2 worldPosition(
    bw::core::arr::FixedPointVertex const& vertex) {
  return {bw::core::arr::ToWorldCoordinate(vertex.x),
          bw::core::arr::ToWorldCoordinate(vertex.y)};
}

void requireNormal(
    std::array<float, 3> const& actual,
    std::array<float, 3> const& expected,
    std::string const& message) {
  for (size_t component = 0; component < actual.size(); ++component) {
    requireNear(actual[component], expected[component], message);
  }
}

void trianglesEvaluateTheirOwningElevationPlanes() {
  PrimitivePropertySet properties;
  properties.floorZ = Elevation{3.0f, {2.0f, -1.0f}};
  properties.ceilingZ = Elevation{20.0f, {-0.5f, 0.25f}};
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(0, 0, 2000, 1000), 0, properties)});
  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);

  require(triangles.size() == 2, "the sloped rectangle did not triangulate");
  auto floorNormal = properties.floorZ.normal();
  auto ceilingUp = properties.ceilingZ.normal();
  std::array<float, 3> ceilingNormal{
      -ceilingUp[0], -ceilingUp[1], -ceilingUp[2]};
  for (auto const& triangle : triangles) {
    requireNormal(
        triangle.floor.normal, floorNormal,
        "a floor triangle did not retain its Elevation plane normal");
    requireNormal(
        triangle.ceiling.normal, ceilingNormal,
        "a ceiling triangle did not retain its outward Elevation plane normal");
    for (size_t corner = 0; corner < 3; ++corner) {
      auto position = worldPosition(arrangement->vertices[triangle.v[corner]]);
      requireNear(
          triangle.floor.elevation[corner], properties.floorZ.evaluate(position),
          "a floor triangle vertex did not evaluate its Elevation plane");
      requireNear(
          triangle.ceiling.elevation[corner],
          properties.ceilingZ.evaluate(position),
          "a ceiling triangle vertex did not evaluate its Elevation plane");
    }
  }

  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  require(walls.size() == 4, "the sloped rectangle did not retain its Borders");
  for (auto const& wall : walls) {
    require(
        wall.kind == ArrangementWallKind::Border,
        "an isolated sloped rectangle produced a non-Border wall");
    auto const& edge = arrangement->edges[wall.edge];
    for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
      auto position = worldPosition(arrangement->vertices[edge.v[endpoint]]);
      requireNear(
          wall.bottomZ[endpoint], properties.floorZ.evaluate(position),
          "a Border bottom did not evaluate its floor plane at the endpoint");
      requireNear(
          wall.topZ[endpoint], properties.ceilingZ.evaluate(position),
          "a Border top did not evaluate its ceiling plane at the endpoint");
    }
  }
}

void booleanSubdivisionPreservesOnePlaneAcrossEveryFragment() {
  PrimitivePropertySet properties;
  properties.floorZ = Elevation{2.0f, {0.75f, -0.25f}};
  properties.ceilingZ = Elevation{30.0f, {-0.1f, 0.2f}};
  auto base = primitive(rectangle(0, 0, 4000, 4000), 0, properties);
  auto subdivision = primitive(rectangle(1500, 1000, 2500, 3000), 1, {});
  subdivision.contributesProperties = false;

  auto arrangement =
      bw::core::arr::BuildArrangement({base, subdivision});
  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  require(arrangement->faces.size() > 2 && triangles.size() > 2,
          "the structural boolean did not subdivide the sloped surface");
  for (size_t faceIndex = 1; faceIndex < arrangement->faces.size(); ++faceIndex) {
    auto const& face = arrangement->faces[faceIndex];
    if (!face.solid) continue;
    require(face.primitiveIndex == 0 && face.contributesProperties,
            "a subdivided fragment changed its property-winning Primitive");
  }
  for (auto const& triangle : triangles) {
    requireNormal(triangle.floor.normal, properties.floorZ.normal(),
                  "a subdivided plane fragment changed its floor normal");
    for (size_t corner = 0; corner < 3; ++corner) {
      auto position = worldPosition(arrangement->vertices[triangle.v[corner]]);
      requireNear(triangle.floor.elevation[corner],
                  properties.floorZ.evaluate(position),
                  "a subdivided plane fragment changed its elevation");
    }
  }
}

void wallsEvaluateBothBoundariesAtBothEdgeEndpoints() {
  PrimitivePropertySet left;
  left.floorZ = Elevation{0.0f, {0.0f, 0.25f}};
  left.ceilingZ = Elevation{24.0f, {0.0f, -0.25f}};
  PrimitivePropertySet right;
  right.floorZ = Elevation{4.0f, {0.0f, 0.5f}};
  right.ceilingZ = Elevation{20.0f, {0.0f, -0.5f}};

  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(-1000, 0, 0, 8000), 0, left),
       primitive(rectangle(0, 0, 1000, 8000), 1, right)});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);

  auto floorStep = std::ranges::find_if(walls, [&](auto const& wall) {
    if (wall.kind != ArrangementWallKind::FloorStep) return false;
    auto const& edge = arrangement->edges[wall.edge];
    return arrangement->faces[edge.face[0]].solid &&
           arrangement->faces[edge.face[1]].solid;
  });
  auto ceilingStep = std::ranges::find_if(walls, [&](auto const& wall) {
    if (wall.kind != ArrangementWallKind::CeilingStep) return false;
    auto const& edge = arrangement->edges[wall.edge];
    return arrangement->faces[edge.face[0]].solid &&
           arrangement->faces[edge.face[1]].solid;
  });
  require(
      floorStep != walls.end() && ceilingStep != walls.end(),
      "the shared edge did not produce both varying Step wall descriptions");

  auto requireEndpoints = [&](auto const& wall, bool floor) {
    auto const& edge = arrangement->edges[wall.edge];
    for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
      auto position = worldPosition(arrangement->vertices[edge.v[endpoint]]);
      auto side0 = floor ? left.floorZ.evaluate(position)
                         : left.ceilingZ.evaluate(position);
      auto side1 = floor ? right.floorZ.evaluate(position)
                         : right.ceilingZ.evaluate(position);
      requireNear(
          wall.bottomZ[endpoint], std::min(side0, side1),
          "a Step wall bottom did not evaluate both planes at its endpoint");
      requireNear(
          wall.topZ[endpoint], std::max(side0, side1),
          "a Step wall top did not evaluate both planes at its endpoint");
    }
  };
  requireEndpoints(*floorStep, true);
  requireEndpoints(*ceilingStep, false);
  require(
      bw::core::arr::BuildArrangementWallSurface(*arrangement, *floorStep)
                  .vertexCount == 4 &&
          bw::core::arr::BuildArrangementWallSurface(
              *arrangement, *ceilingStep)
                  .vertexCount == 4,
      "varying non-crossing Step walls were not quadrilateral surfaces");
}

void crossingStepPlanesProduceOwnedOrientedTriangularSegments() {
  PrimitivePropertySet left;
  left.floorZ = Elevation{0.0f, {0.0f, 1.0f}};
  left.ceilingZ = Elevation{30.0f, {0.0f, 1.0f}};
  left.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("left.wall");
  PrimitivePropertySet right;
  right.floorZ = Elevation{10.0f, {0.0f, -1.0f}};
  right.ceilingZ = Elevation{40.0f, {0.0f, -1.0f}};
  right.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("right.wall");

  auto leftPrimitive =
      primitive(rectangle(-1000, 0, 0, 10000), 0, left);
  leftPrimitive.contourEdgeOverrides = {
      {std::nullopt, false, std::nullopt, std::nullopt}};
  leftPrimitive.contourEdgeVisibleOverrides = {
      {std::nullopt, false, std::nullopt, std::nullopt}};
  leftPrimitive.contourEdgeNormalMapOverrides = {{std::nullopt, bw::core::WallNormalMapOverride::disabled(),
                                                  std::nullopt, std::nullopt}};
  leftPrimitive.contourEdgeWallMaskOverrides = {{std::nullopt, bw::core::WallMaskOverride::disabled(),
                                                 std::nullopt, std::nullopt}};

  auto arrangement = bw::core::arr::BuildArrangement(
      {leftPrimitive,
       primitive(rectangle(0, 0, 1000, 10000), 1, right)});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  std::vector<bw::core::arr::ArrangementWall const*> floorSegments;
  std::vector<bw::core::arr::ArrangementWall const*> ceilingSegments;
  for (auto const& wall : walls) {
    auto const& edge = arrangement->edges[wall.edge];
    if (!arrangement->faces[edge.face[0]].solid ||
        !arrangement->faces[edge.face[1]].solid) {
      continue;
    }
    if (wall.kind == ArrangementWallKind::FloorStep) {
      floorSegments.push_back(&wall);
    } else if (wall.kind == ArrangementWallKind::CeilingStep) {
      ceilingSegments.push_back(&wall);
    }
  }
  require(floorSegments.size() == 2 && ceilingSegments.size() == 2,
          "crossing adjacent planes did not split both Step surfaces");

  auto verifySegments = [&](auto const& segments, float crossingZ,
                            bool floor) {
    requireNear(segments[0]->sourceEdgeParameter[0], 0.0f,
                "the first derived segment did not start at its source edge");
    requireNear(segments[0]->sourceEdgeParameter[1], 0.5f,
                "the first derived segment did not end at the plane crossing");
    requireNear(segments[1]->sourceEdgeParameter[0], 0.5f,
                "the second derived segment did not start at the plane crossing");
    requireNear(segments[1]->sourceEdgeParameter[1], 1.0f,
                "the second derived segment did not end at its source edge");

    for (auto const* segment : segments) {
      require(!segment->visible,
              "a derived Step segment lost its visibility override");
      require(
          arrangement->edges[segment->edge].collidesOverride == false,
          "a derived Step segment lost its collision override");
      require(segment->normalMapOverride.state() ==
                      bw::core::WallNormalMapOverride::State::Disabled &&
                  segment->wallMaskOverride.state() ==
                      bw::core::WallMaskOverride::State::Disabled,
              "a derived Step segment lost an inherited Image override");
      auto surface =
          bw::core::arr::BuildArrangementWallSurface(*arrangement, *segment);
      require(surface.vertexCount == 3,
              "a Step segment ending at a plane crossing was not triangular");
      require(std::ranges::any_of(surface.vertices.begin(),
                                  surface.vertices.begin() +
                                      surface.vertexCount,
                                  [&](auto const& vertex) {
                                    return std::abs(vertex.position.y - 5.0f) <
                                               Epsilon &&
                                           std::abs(vertex.elevation -
                                                    crossingZ) < Epsilon;
                                  }),
              "a triangular Step segment did not retain its crossing tip");
      require(segment->ownerFace < arrangement->faces.size() &&
                  segment->frontFace < arrangement->faces.size() &&
                  segment->ownerFace != segment->frontFace,
              "a derived Step segment did not identify its two sides");
      auto const& owner = arrangement->palette[arrangement->faces[segment->ownerFace].paletteIndex];
      require(owner.wallMaterial ==
                  arrangement->palette[segment->paletteIndex].wallMaterial,
              "a derived Step segment did not use its owner's material");
      auto orientation =
          bw::core::arr::OrientArrangementWall(*arrangement, *segment);
      auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
      auto const& front = arrangement->palette[arrangement->faces[segment->frontFace].paletteIndex];
      auto const& behind = arrangement->palette[arrangement->faces[segment->ownerFace].paletteIndex];
      auto frontHeight = floor ? front.floorZ.evaluate(midpoint)
                               : front.ceilingZ.evaluate(midpoint);
      auto behindHeight = floor ? behind.floorZ.evaluate(midpoint)
                                : behind.ceilingZ.evaluate(midpoint);
      require(floor ? frontHeight < behindHeight
                    : frontHeight > behindHeight,
              "a derived Step segment faced the wrong adjacent plane");
    }

    auto firstOrientation = bw::core::arr::OrientArrangementWall(
        *arrangement, *segments[0]);
    auto secondOrientation = bw::core::arr::OrientArrangementWall(
        *arrangement, *segments[1]);
    require(firstOrientation.normal.dot(secondOrientation.normal) < -0.99f,
            "Step segments did not reverse orientation where ordering crossed");
    require(segments[0]->paletteIndex != segments[1]->paletteIndex,
            "Step segments did not change material owner at the crossing");
  };
  verifySegments(floorSegments, 5.0f, true);
  verifySegments(ceilingSegments, 35.0f, false);
}

void planesCoincidentAlongAnEdgeProduceNoDegenerateStep() {
  PrimitivePropertySet left;
  left.floorZ = Elevation{0.0f, {1.0f, 0.0f}};
  left.ceilingZ = 20.0f;
  PrimitivePropertySet right;
  right.floorZ = 0.0f;
  right.ceilingZ = 20.0f;
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(-1000, 0, 0, 10000), 0, left),
       primitive(rectangle(0, 0, 1000, 10000), 1, right)});
  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  require(std::ranges::none_of(walls, [&](auto const& wall) {
            auto const& edge = arrangement->edges[wall.edge];
            return wall.kind == ArrangementWallKind::FloorStep &&
                   arrangement->faces[edge.face[0]].solid &&
                   arrangement->faces[edge.face[1]].solid;
          }),
          "planes equal along their source edge produced a degenerate Step");
}

void generatedRegionsRejectCrossedPlanesButPermitEquality() {
  PrimitivePropertySet crossed;
  crossed.floorZ = Elevation{0.0f, {1.0f, 0.0f}};
  crossed.ceilingZ = 5.0f;

  bool rejected = false;
  try {
    (void)bw::core::arr::BuildArrangement(
        {primitive(rectangle(0, 0, 10000, 10000), 0, crossed)});
  } catch (bw::core::CoreException const& error) {
    auto const message = std::string(error.what());
    rejected = message.find("floor elevation exceeds ceiling elevation") !=
               std::string::npos;
  }
  require(rejected, "a region whose sloped floor crossed its ceiling was not clearly rejected");

  PrimitivePropertySet touching;
  touching.floorZ = Elevation{0.0f, {0.5f, 0.0f}};
  touching.ceilingZ = 5.0f;
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(0, 0, 10000, 10000), 0, touching)});
  require(arrangement->faces.size() == 2 && arrangement->faces[1].solid,
          "zero clearance at a boundary vertex was rejected");
}

void nonzeroLiquidWithSlopedGeneratedSurfacesIsAccepted() {
  PrimitivePropertySet properties;
  properties.floorZ = Elevation{0.0f, {0.1f, 0.0f}};
  properties.ceilingZ = 20.0f;
  properties.liquidLevel = 1.0f;

  auto liquidPrimitive =
      primitive(rectangle(0, 0, 10000, 10000), 0, properties);
  liquidPrimitive.rawArea = 100.0;
  auto arrangement =
      bw::core::arr::BuildArrangement({liquidPrimitive});
  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  auto liquid = bw::core::arr::ComputeLiquidState(*arrangement, triangles);
  require(!liquid.surfaceTriangles.empty(),
          "Liquid on a sloped generated surface did not settle");
  require(liquid.faceDepths[1] == 0.0f,
          "the flat compatibility API invented one depth for a sloped face");
}

void elevationCrossingsDoNotAlterExactArrangementTopology() {
  PrimitivePropertySet flat;
  flat.floorZ = 0.0f;
  flat.ceilingZ = 20.0f;
  auto flatArrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(-1000, 0, 0, 10000), 0, flat),
       primitive(rectangle(0, 0, 1000, 10000), 1, flat)});

  auto rising = flat;
  rising.floorZ = Elevation{0.0f, {0.0f, 1.0f}};
  auto falling = flat;
  falling.floorZ = Elevation{10.0f, {0.0f, -1.0f}};
  auto slopedArrangement = bw::core::arr::BuildArrangement(
      {primitive(rectangle(-1000, 0, 0, 10000), 0, rising),
       primitive(rectangle(0, 0, 1000, 10000), 1, falling)});

  require(
      slopedArrangement->vertices == flatArrangement->vertices,
      "an elevation-only crossing changed fixed-point Arrangement vertices");
  require(
      slopedArrangement->edges.size() == flatArrangement->edges.size() &&
          slopedArrangement->faces.size() == flatArrangement->faces.size(),
      "an elevation-only crossing changed Arrangement topology counts");
  for (size_t index = 0; index < flatArrangement->edges.size(); ++index) {
    auto const& flatEdge = flatArrangement->edges[index];
    auto const& slopedEdge = slopedArrangement->edges[index];
    require(
        flatEdge.v[0] == slopedEdge.v[0] &&
            flatEdge.v[1] == slopedEdge.v[1] &&
            flatEdge.face[0] == slopedEdge.face[0] &&
            flatEdge.face[1] == slopedEdge.face[1],
        "an elevation-only crossing changed exact edge incidence");
  }
  for (size_t index = 0; index < flatArrangement->faces.size(); ++index) {
    auto const& flatFace = flatArrangement->faces[index];
    auto const& slopedFace = slopedArrangement->faces[index];
    require(
        flatFace.outerBoundary == slopedFace.outerBoundary &&
            flatFace.outerBoundaryVertices ==
                slopedFace.outerBoundaryVertices &&
            flatFace.innerBoundaries == slopedFace.innerBoundaries &&
            flatFace.innerBoundaryVertices ==
                slopedFace.innerBoundaryVertices &&
            flatFace.solid == slopedFace.solid,
        "an elevation-only crossing changed exact face boundaries");
  }

  auto walls = bw::core::arr::BuildArrangementWalls(*slopedArrangement);
  auto crossingWall = std::ranges::find_if(walls, [&](auto const& wall) {
    if (wall.kind != ArrangementWallKind::FloorStep) return false;
    auto const& edge = slopedArrangement->edges[wall.edge];
    return slopedArrangement->faces[edge.face[0]].solid &&
           slopedArrangement->faces[edge.face[1]].solid;
  });
  require(
      crossingWall != walls.end(),
      "the elevation-only crossing was not retained in derived wall output");
  require(
      crossingWall->edge < slopedArrangement->edges.size(),
      "derived wall output did not retain its exact source edge");
}

}  // namespace

int main() {
  try {
    trianglesEvaluateTheirOwningElevationPlanes();
    booleanSubdivisionPreservesOnePlaneAcrossEveryFragment();
    wallsEvaluateBothBoundariesAtBothEdgeEndpoints();
    crossingStepPlanesProduceOwnedOrientedTriangularSegments();
    planesCoincidentAlongAnEdgeProduceNoDegenerateStep();
    generatedRegionsRejectCrossedPlanesButPermitEquality();
    nonzeroLiquidWithSlopedGeneratedSurfacesIsAccepted();
    elevationCrossingsDoNotAlterExactArrangementTopology();
    std::cout << "Arrangement surface geometry evaluates Elevation planes without changing topology\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
