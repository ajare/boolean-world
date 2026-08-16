#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementResult;
using bw::core::arr::Contour;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ArrangementPrimitive squareWithDanglingSpur() {
  // The doubled-back segment is a dangling arrangement edge. Both half-edges
  // are incident to the square's face, but their boundary occurrences start
  // at opposite vertices.
  Contour contour{{0, 0},
                  {4000, 0},
                  {4000, 4000},
                  {0, 4000},
                  {0, 2000},
                  {2000, 2000},
                  {0, 2000}};
  return {{std::move(contour)},
          Primitive::Operation::Union,
          Primitive::FillRule::EvenOdd,
          0,
          23};
}

int64_t triangleArea2(
    bw::core::arr::ArrangementTriangle const& triangle,
    ArrangementResult const& arrangement) {
  auto const& a = arrangement.vertices[triangle.v[0]];
  auto const& b = arrangement.vertices[triangle.v[1]];
  auto const& c = arrangement.vertices[triangle.v[2]];
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

void preservesTraversalAcrossAnEdgeIncidentToTheSameFace() {
  auto arrangement =
      bw::core::arr::BuildArrangement({squareWithDanglingSpur()});

  require(arrangement->faces.size() == 2,
          "the spur contour should produce one bounded face");
  auto const& face = arrangement->faces[1];
  require(face.solid, "the bounded face should remain solid");
  require(face.outerBoundary.size() == face.outerBoundaryVertices.size(),
          "each boundary edge should retain its traversal vertex");

  auto sameFaceEdge = std::ranges::find_if(
      arrangement->edges,
      [](auto const& edge) { return edge.face[0] == edge.face[1]; });
  require(sameFaceEdge != arrangement->edges.end(),
          "the doubled-back segment should be incident to the same face");
  auto sameFaceEdgeIndex =
      uint32_t(std::distance(arrangement->edges.begin(), sameFaceEdge));

  std::vector<uint32_t> occurrenceVertices;
  for (size_t i = 0; i < face.outerBoundary.size(); ++i) {
    if (face.outerBoundary[i] == sameFaceEdgeIndex) {
      occurrenceVertices.push_back(face.outerBoundaryVertices[i]);
    }
  }
  require(occurrenceVertices.size() == 2,
          "the dangling edge should occur twice in the face boundary");
  require(occurrenceVertices[0] != occurrenceVertices[1],
          "opposite boundary traversals should not select the same endpoint");

  auto triangles = bw::core::arr::BuildArrangementTriangles(*arrangement);
  require(!triangles.empty(),
          "triangulating the square with a spur should produce triangles");
  int64_t area2 = 0;
  for (auto const& triangle : triangles) {
    auto area = triangleArea2(triangle, *arrangement);
    require(area != 0, "the same-face edge produced a degenerate triangle");
    area2 += std::abs(area);
  }
  require(area2 == 32'000'000,
          "the triangulated area should equal the full square");
}

void preservesTraversalWhenBakingAMeshPrimitive() {
  bw::core::ComplexPolygon polygon{{{{0.0f, 0.0f}},
                                    {{4.0f, 0.0f}},
                                    {{4.0f, 4.0f}},
                                    {{0.0f, 4.0f}},
                                    {{0.0f, 2.0f}},
                                    {{2.0f, 2.0f}},
                                    {{0.0f, 2.0f}}}};
  auto source = std::unique_ptr<bw::core::MeshPrimitive>(
      bw::core::MeshPrimitive::fromComplexPolygons(
          Primitive::Operation::Union,
          Primitive::FillRule::EvenOdd,
          {std::move(polygon)}));
  source->updateVertexPositions();

  bw::core::World world(10.0f, 1.0f);
  auto baked = std::unique_ptr<Primitive>(
      world.createMeshPrimitive({source.get()}));
  require(baked != nullptr,
          "baking the same-face edge arrangement should produce a primitive");
  baked->updateVertexPositions();
  require(baked->getVertices().size() == 1 &&
              baked->getVertices()[0].size() == 1,
          "baking should preserve the arrangement face as one complex polygon");
  require(baked->getVertices()[0][0].size() == 7,
          "baking should preserve both traversals of the dangling edge");

  auto const& bakedContour = baked->getVertices()[0][0];
  auto spurTip = std::ranges::find_if(bakedContour, [](auto const& vertex) {
    return vertex.p.x == 2.0f && vertex.p.y == 2.0f;
  });
  require(spurTip != bakedContour.end(),
          "baking should retain the dangling edge's tip");
  auto tipIndex = size_t(std::distance(bakedContour.begin(), spurTip));
  auto const& previous = bakedContour[(tipIndex + bakedContour.size() - 1) % bakedContour.size()];
  auto const& next = bakedContour[(tipIndex + 1) % bakedContour.size()];
  require(previous.p.x == 0.0f && previous.p.y == 2.0f &&
              next.p.x == 0.0f && next.p.y == 2.0f,
          "baking should retain both directions of the dangling edge");
}

}  // namespace

int main() {
  try {
    preservesTraversalAcrossAnEdgeIncidentToTheSameFace();
    preservesTraversalWhenBakingAMeshPrimitive();
    std::cout << "Same-face arrangement edges retain boundary traversal order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
