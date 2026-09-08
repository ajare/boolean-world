#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/PrimitiveFactory.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main() {
  using namespace bw::core;

  std::array<float, 6> superformulaValues{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  std::vector<std::pair<PrimitiveShapeSpec, std::string>> shapes{
      {RegularPolygonSpec{5}, "Regular"},
      {CircleSpec{0.25f}, "Circle"},
      {CircleSegmentSpec{120.0f, 0.3f}, "CircleSegment"},
      {TorusSpec{0.4f, 0.35f}, "Torus"},
      {TorusSegmentSpec{0.45f, 150.0f, 0.4f}, "TorusSegment"},
      {RectangleSpec{2.5f}, "Rectangle"},
      {SuperformulaSpec{superformulaValues, 0.45f}, "Superformula"},
  };

  for (auto const& [shape, expectedType] : shapes) {
    auto primitive = PrimitiveFactory::create({shape, Primitive::Operation::Difference, Primitive::FillRule::EvenOdd, 17, {12.0f, -4.0f}, 3.0f, 42.0f});

    require(primitive->getType() == expectedType,
            "factory created the wrong Primitive type");
    require(primitive->getOperation() == Primitive::Operation::Difference &&
                primitive->getFillRule() == Primitive::FillRule::EvenOdd &&
                primitive->getPriority() == 17 &&
                primitive->getPosition() == wp::Vector2{12.0f, -4.0f} &&
                primitive->getSize() == wp::Vector2{3.0f, 3.0f},
            "factory did not apply the shared Primitive specification");

    auto const& angle = primitive->getAnimationInterpolator(
        VertexTransformer::Key::Angle);
    require(angle.getValue(0.0f) == 42.0f &&
                angle.getValue(1.0f) == 42.0f,
            "factory did not initialize the authored angle");
  }

  std::cout << "primitive_factory_tests passed\n";
  return 0;
}
