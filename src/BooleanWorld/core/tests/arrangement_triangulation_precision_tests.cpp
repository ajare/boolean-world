#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::arr::ArrangementFace;
using bw::core::arr::ArrangementResult;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::Membership;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int64_t triangleArea2(FixedPointVertex const& a,
                      FixedPointVertex const& b,
                      FixedPointVertex const& c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

std::vector<FixedPointVertex> narrowWorldEdgeSquare() {
  // Adjacent fixed-point vertices at the positive world edge must remain
  // distinct while earcut chooses the triangulation.
  return {{4'095'999, 4'095'999},
          {4'096'000, 4'095'999},
          {4'096'000, 4'096'000},
          {4'095'999, 4'096'000}};
}

void triangulatesArrangementAtFixedPointPrecision() {
  ArrangementResult arrangement;
  arrangement.vertices = narrowWorldEdgeSquare();
  ArrangementFace face{{0, 1, 2, 3},
                       {0, 1, 2, 3},
                       {},
                       {},
                       Membership(0),
                       true};
  arrangement.faces.emplace_back(
      ArrangementFace{{}, {}, {}, {}, Membership(0)});
  arrangement.faces.emplace_back(std::move(face));

  auto triangles = bw::core::arr::BuildArrangementTriangles(arrangement);
  require(triangles.size() == 2,
          "a one-grid-quantum square at the world edge should triangulate");

  int64_t area2 = 0;
  for (auto const& triangle : triangles) {
    area2 += std::abs(triangleArea2(arrangement.vertices[triangle.v[0]],
                                    arrangement.vertices[triangle.v[1]],
                                    arrangement.vertices[triangle.v[2]]));
  }
  require(area2 == 2,
          "triangulation should preserve the one-grid-quantum square area");
}
}  // namespace

int main() {
  try {
    triangulatesArrangementAtFixedPointPrecision();
    std::cout << "Arrangement triangulation preserves fixed-point precision\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
