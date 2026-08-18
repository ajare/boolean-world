#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>

namespace {

constexpr float Epsilon = 0.0001f;
constexpr float BoundaryEpsilon = 0.001f;

using bw::core::ClosedPolygon;
using bw::core::Primitive;
using bw::core::TorusPolygon;
using bw::core::TorusSegmentPolygon;
using bw::core::Vertex;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

float radius(Vertex const& vertex) {
  return std::sqrt(vertex.p.x * vertex.p.x + vertex.p.y * vertex.p.y);
}

float distance(Vertex const& left, Vertex const& right) {
  float const deltaX = left.p.x - right.p.x;
  float const deltaY = left.p.y - right.p.y;
  return std::sqrt(deltaX * deltaX + deltaY * deltaY);
}

void requireRadius(ClosedPolygon const& contour, float expectedRadius, std::string const& message) {
  for (auto const& vertex : contour) {
    require(std::abs(radius(vertex) - expectedRadius) < Epsilon,
            message + ": expected " + std::to_string(expectedRadius) + ", got " +
                std::to_string(radius(vertex)));
  }
}

void requireEqualContours(ClosedPolygon const& left, ClosedPolygon const& right, std::string const& message) {
  require(left.size() == right.size(), message);
  for (size_t index = 0; index < left.size(); ++index) {
    require(distance(left[index], right[index]) < Epsilon, message);
  }
}

void torusAndSegmentsUseThicknessAsRadialWidth(float thickness) {
  auto const operation = Primitive::Operation::Union;
  auto const fillRule = Primitive::FillRule::NonZero;

  TorusPolygon torus(operation, fillRule, thickness, 1.0f);
  TorusSegmentPolygon partialSegment(operation, fillRule, thickness, 180.0f, 1.0f);
  TorusSegmentPolygon fullSegment(operation, fillRule, thickness, 360.0f, 1.0f);
  torus.setSize(1.0f, 1.0f);
  partialSegment.setSize(1.0f, 1.0f);
  fullSegment.setSize(1.0f, 1.0f);
  torus.updateVertexPositions();
  partialSegment.updateVertexPositions();
  fullSegment.updateVertexPositions();

  auto const& torusContours = torus.getVertices().front();
  require(torusContours.size() == 2, "torus did not generate outer and inner contours");
  float const outerRadius = radius(torusContours[0].front());
  float const innerRadius = outerRadius * (1.0f - thickness);
  requireRadius(torusContours[0], outerRadius, "torus outer radius changed");
  requireRadius(torusContours[1], innerRadius, "torus inner radius did not equal one minus thickness");

  auto const& partialContour = partialSegment.getVertices().front().front();
  size_t const outerVertexCount = partialSegment.getNumSides() + 1;
  requireRadius(
      ClosedPolygon(partialContour.begin(), partialContour.begin() + outerVertexCount),
      outerRadius,
      "partial torus segment outer radius changed");
  requireRadius(
      ClosedPolygon(partialContour.begin() + outerVertexCount, partialContour.end()),
      innerRadius,
      "partial torus segment inner radius did not equal one minus thickness");

  auto const& fullContours = fullSegment.getVertices().front();
  require(fullContours.size() == 2, "full torus segment did not generate outer and inner contours");
  requireRadius(fullContours[0], outerRadius, "full torus segment outer radius changed");
  requireRadius(fullContours[1], innerRadius, "full torus segment inner radius did not equal one minus thickness");
}

void geometryIsContinuousAtNormalizedFullArcBoundary(float thickness) {
  auto const operation = Primitive::Operation::Union;
  auto const fillRule = Primitive::FillRule::NonZero;

  TorusSegmentPolygon below(operation, fillRule, thickness, 359.99f, 1.0f);
  TorusSegmentPolygon at(operation, fillRule, thickness, 360.0f, 1.0f);
  TorusSegmentPolygon above(operation, fillRule, thickness, 360.01f, 1.0f);
  below.setSize(1.0f, 1.0f);
  at.setSize(1.0f, 1.0f);
  above.setSize(1.0f, 1.0f);
  below.updateVertexPositions();
  at.updateVertexPositions();
  above.updateVertexPositions();

  auto const& belowContour = below.getVertices().front().front();
  auto const& atContours = at.getVertices().front();
  auto const& aboveContours = above.getVertices().front();
  size_t const oppositeVertex = at.getNumSides() / 2;

  require(std::abs(above.getArcLength() - 360.0f) < Epsilon,
          "arc lengths above 360 degrees were not normalized");
  require(distance(belowContour.front(), atContours[0][oppositeVertex]) < BoundaryEpsilon,
          "outer geometry was discontinuous immediately below 360 degrees: " +
              std::to_string(distance(belowContour.front(), atContours[0][oppositeVertex])));
  require(distance(belowContour.back(), atContours[1][oppositeVertex - 1]) < BoundaryEpsilon,
          "inner geometry was discontinuous immediately below 360 degrees: " +
              std::to_string(distance(belowContour.back(), atContours[1][oppositeVertex - 1])));
  requireEqualContours(atContours[0], aboveContours[0],
                       "outer geometry changed immediately above 360 degrees");
  requireEqualContours(atContours[1], aboveContours[1],
                       "inner geometry changed immediately above 360 degrees");
}

}  // namespace

int main() {
  try {
    for (float thickness : {0.2f, 0.7f}) {
      torusAndSegmentsUseThicknessAsRadialWidth(thickness);
      geometryIsContinuousAtNormalizedFullArcBoundary(thickness);
    }
    std::cout << "Torus thickness is consistent for partial and full arcs\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
