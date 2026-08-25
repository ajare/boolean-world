#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/MeshPrimitive.h>

#include "PrimitivePreviewGeometry.h"

namespace {

using bw::core::ClosedPolygon;
using bw::core::MeshFilledRegion;
using bw::core::MeshHole;
using bw::core::MeshPrimitive;
using bw::core::Primitive;
using editor::PrimitivePreviewGeometry;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool near(float first, float second) {
  return std::abs(first - second) < 0.001f;
}

ClosedPolygon square(float left, float bottom, float right, float top) {
  return {{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}};
}

std::unique_ptr<MeshPrimitive> makePrimitive(
    std::vector<MeshFilledRegion> shells,
    float floorZ = 3.0f,
    float ceilingZ = 27.0f) {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, std::move(shells)));
  auto properties = primitive->getProperties();
  properties.floorZ = floorZ;
  properties.ceilingZ = ceilingZ;
  properties.floorMaterialId = "test.floor";
  properties.ceilingMaterialId = "test.ceiling";
  properties.wallMaterialId = "test.wall";
  primitive->setProperties(properties);
  return primitive;
}

float triangleArea(editor::PreviewTriangle const& triangle) {
  auto const& a = triangle.vertices[0];
  auto const& b = triangle.vertices[1];
  auto const& c = triangle.vertices[2];
  return std::abs(
             (b.x - a.x) * (c.y - a.y) -
             (b.y - a.y) * (c.x - a.x)) *
         0.5f;
}

float totalArea(std::vector<editor::PreviewTriangle> const& triangles) {
  float result = 0.0f;
  for (auto const& triangle : triangles) {
    result += triangleArea(triangle);
  }
  return result;
}

bool hasWallAlong(
    PrimitivePreviewGeometry const& geometry,
    float x0,
    float y0,
    float x1,
    float y1) {
  for (auto const& wall : geometry.wallQuads) {
    auto const& a = wall.vertices[0];
    auto const& b = wall.vertices[1];
    bool forward = near(a.x, x0) && near(a.y, y0) &&
                   near(b.x, x1) && near(b.y, y1);
    bool reverse = near(a.x, x1) && near(a.y, y1) &&
                   near(b.x, x0) && near(b.y, y0);
    if (forward || reverse) {
      return true;
    }
  }
  return false;
}

void singleShellProducesFillsAndEveryBoundaryWall() {
  auto primitive = makePrimitive({{square(0, 0, 10, 10), {}}});
  auto geometry = editor::extrudePrimitiveForPreview(*primitive);

  require(near(totalArea(geometry.floorTriangles), 100.0f),
          "single-Shell floor fill did not cover the Shell");
  require(near(totalArea(geometry.ceilingTriangles), 100.0f),
          "single-Shell ceiling fill did not cover the Shell");
  require(geometry.wallQuads.size() == 4,
          "single-Shell extrusion did not loft all four Ring edges");
  for (auto const& wall : geometry.wallQuads) {
    require(near(wall.vertices[0].z, 3.0f) &&
                near(wall.vertices[1].z, 3.0f) &&
                near(wall.vertices[2].z, 27.0f) &&
                near(wall.vertices[3].z, 27.0f),
            "wall quad did not span the Primitive floorZ to ceilingZ");
  }
  for (auto const& triangle : geometry.floorTriangles) {
    for (auto const& vertex : triangle.vertices) {
      require(
          near(vertex.nx, 0.0f) && near(vertex.ny, 0.0f) &&
              near(vertex.nz, 1.0f),
          "floor triangle normal did not point up");
      require(
          near(vertex.u, vertex.x) && near(vertex.v, vertex.y),
          "floor triangle UV was not the world-space position");
    }
  }
  for (auto const& triangle : geometry.ceilingTriangles) {
    for (auto const& vertex : triangle.vertices) {
      require(
          near(vertex.nx, 0.0f) && near(vertex.ny, 0.0f) &&
              near(vertex.nz, -1.0f),
          "ceiling triangle normal did not point down");
    }
  }
  for (auto const& wall : geometry.wallQuads) {
    auto const& a = wall.vertices[0];
    auto const& b = wall.vertices[1];
    auto edgeLength = std::sqrt(
        (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    require(
        near(a.nx * a.nx + a.ny * a.ny, 1.0f) && near(a.nz, 0.0f),
        "wall quad normal was not a unit vector in the ground plane");
    require(
        near(a.u, 0.0f) && near(a.v, 0.0f) && near(b.u, edgeLength) &&
            near(b.v, 0.0f),
        "wall quad UV did not run along the edge and rise with height");
  }
}

void holeIsExcludedFromFillsAndGetsItsOwnWalls() {
  MeshFilledRegion shell{square(0, 0, 10, 10), {}};
  shell.holes.push_back({square(2, 2, 8, 8), {}});
  auto primitive = makePrimitive({shell});
  auto geometry = editor::extrudePrimitiveForPreview(*primitive);

  require(near(totalArea(geometry.floorTriangles), 64.0f) &&
              near(totalArea(geometry.ceilingTriangles), 64.0f),
          "Hole interior was not excluded from horizontal fills");
  require(geometry.wallQuads.size() == 8,
          "Hole Ring did not receive an additional wall loft");
  require(hasWallAlong(geometry, 2, 2, 8, 2),
          "Hole boundary wall geometry was absent");
}

void islandLoftsAllThreeContainmentLevels() {
  MeshFilledRegion island{square(4, 4, 6, 6), {}};
  MeshHole hole{square(2, 2, 8, 8), {island}};
  MeshFilledRegion shell{square(0, 0, 10, 10), {hole}};
  auto primitive = makePrimitive({shell});
  auto geometry = editor::extrudePrimitiveForPreview(*primitive);

  require(geometry.wallQuads.size() == 12,
          "Shell, Hole and Island Rings were not all lofted");
  require(near(totalArea(geometry.floorTriangles), 68.0f),
          "Island was not restored inside the Hole's excluded fill");
  require(hasWallAlong(geometry, 4, 4, 6, 4),
          "Island boundary wall geometry was absent");
}

void sharedEdgesRemainIndependentWalls() {
  auto left = makePrimitive({{square(0, 0, 10, 10), {}}});
  auto right = makePrimitive({{square(10, 0, 20, 10), {}}});
  auto leftGeometry = editor::extrudePrimitiveForPreview(*left);
  auto rightGeometry = editor::extrudePrimitiveForPreview(*right);

  require(leftGeometry.wallQuads.size() == 4 &&
              rightGeometry.wallQuads.size() == 4,
          "independent Primitive wall geometry was suppressed or merged");
  require(hasWallAlong(leftGeometry, 10, 0, 10, 10) &&
              hasWallAlong(rightGeometry, 10, 0, 10, 10),
          "each Primitive did not retain its own copy of the shared edge wall");
}

}  // namespace

int main() {
  try {
    singleShellProducesFillsAndEveryBoundaryWall();
    holeIsExcludedFromFillsAndGetsItsOwnWalls();
    islandLoftsAllThreeContainmentLevels();
    sharedEdgesRemainIndependentWalls();
    std::cout << "Primitive preview geometry tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
