#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementResult;
using bw::core::arr::Contour;
using bw::core::arr::ContourInput;
using bw::core::arr::FixedPointVertex;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

constexpr int contourVertexCount = 32;

Contour circleContour(int64_t radius) {
  Contour contour;
  contour.reserve(contourVertexCount);
  for (int i = 0; i < contourVertexCount; ++i) {
    auto angle = 2.0 * std::numbers::pi * double(i) /
                 double(contourVertexCount);
    contour.push_back({int64_t(std::llround(double(radius) * std::cos(angle))),
                       int64_t(std::llround(double(radius) * std::sin(angle)))});
  }
  return contour;
}

std::vector<ContourInput> nestedContours() {
  return {{circleContour(100), 0},
          {circleContour(60), 0},
          {circleContour(20), 1}};
}

std::vector<ArrangementPrimitive> nestedSolidPrimitives() {
  return {{{circleContour(100), circleContour(60)},
           Primitive::Operation::Union,
           Primitive::FillRule::EvenOdd,
           0,
           10},
          {{circleContour(20)},
           Primitive::Operation::Union,
           Primitive::FillRule::EvenOdd,
           1,
           11}};
}

int containingFace(
    FixedPointVertex const& point,
    ArrangementResult const& arrangement) {
  for (int i = 1; i < int(arrangement.faces.size()); ++i) {
    if (bw::core::arr::PointInFace(
            point, arrangement.faces[i], arrangement)) {
      return i;
    }
  }
  return -1;
}

void filtersClockwiseLeafBoundaryDuringExtraction() {
  auto graph = bw::core::arr::BuildPSLG(nestedContours());
  auto cycles = bw::core::arr::ExtractMinimalCycles(graph);

  require(cycles.size() == 3,
          "nested contours should extract only their three bounded cycles");
  require(std::ranges::all_of(cycles, [](auto const& cycle) {
            return cycle.area > 0;
          }),
          "cycle extraction retained a non-positive leaf boundary");
}

void preservesDiscNestedInsideSolidAnnulus() {
  auto arrangement =
      bw::core::arr::BuildArrangement(nestedSolidPrimitives());

  require(arrangement->faces.size() == 4,
          "nested annulus and disc should produce three bounded faces");

  auto annulusFace = containingFace({80, 0}, *arrangement);
  auto gapFace = containingFace({40, 0}, *arrangement);
  auto discFace = containingFace({0, 0}, *arrangement);
  require(annulusFace > 0 && arrangement->faces[annulusFace].solid,
          "the annulus face should remain solid");
  require(gapFace > 0 && !arrangement->faces[gapFace].solid,
          "the gap between annulus and disc should remain empty");
  require(discFace > 0 && arrangement->faces[discFace].solid,
          "the nested disc face should remain solid");
  require(containingFace({120, 0}, *arrangement) < 0,
          "a point outside the arrangement should be in the exterior face");

  auto solidFaces = std::ranges::count_if(
      arrangement->faces,
      [](auto const& face) { return face.solid; });
  require(solidFaces == 2,
          "the arrangement should contain exactly the annulus and disc as solid faces");

  auto walls = bw::core::arr::BuildArrangementWalls(*arrangement);
  require(walls.size() == contourVertexCount * 3,
          "all three nested circular boundaries should produce border walls");
  require(std::ranges::all_of(walls, [](auto const& wall) {
            return wall.kind ==
                   bw::core::arr::ArrangementWallKind::Border;
          }),
          "nested solid boundaries should produce only border walls");
}

}  // namespace

int main() {
  try {
    filtersClockwiseLeafBoundaryDuringExtraction();
    preservesDiscNestedInsideSolidAnnulus();
    std::cout << "Nested leaf boundaries are filtered without changing arrangement behavior\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
