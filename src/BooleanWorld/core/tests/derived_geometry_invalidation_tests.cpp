#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/MeshPrimitive.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon,
          message + ": expected " + std::to_string(expected) +
              ", got " + std::to_string(actual));
}

bw::core::MeshPrimitive makeAsymmetricMesh() {
  std::vector<bw::core::ComplexPolygon> const polygons{
      {{{{0.0f, 0.0f}}, {{2.0f, 0.0f}}, {{0.0f, 1.0f}}}}};
  bw::core::MeshPrimitive primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      polygons);
  primitive.setAnimationValues(bw::core::VertexTransformer::Key::Scale,
                               {{0.0f, 1.0f}, {1.0f, 1.0f}});
  primitive.setSize(1.0f, 1.0f);
  primitive.setFlags(primitive.getFlags() | BW_PRIMITIVE_EXACT_BOUNDS_FLAG);
  primitive.setPosition({3.0f, 4.0f});
  primitive.updateVertexPositions();
  return primitive;
}

void orientationInvalidatesExactBounds() {
  auto primitive = makeAsymmetricMesh();
  auto const originalBounds = primitive.getBounds();
  primitive.setOrientation(90.0f);

  auto const& bounds = primitive.getBounds();
  requireNear(bounds.getWidth(), originalBounds.getHeight(),
              "orientation did not refresh the exact bounds width");
  requireNear(bounds.getHeight(), originalBounds.getWidth(),
              "orientation did not refresh the exact bounds height");
}

void rotatedCopyRefreshesTransformedVertices() {
  auto source = makeAsymmetricMesh();
  auto rotated = std::unique_ptr<bw::core::Primitive>(source.rotatedCopy(90.0f));

  auto const& vertices = rotated->getVertices();
  require(vertices.size() == 1 && vertices[0].size() == 1 &&
              vertices[0][0].size() == 3,
          "rotated copy did not retain its contour");

  auto const& sourceContour = source.getVertices()[0][0];
  auto const& contour = vertices[0][0];
  for (size_t i = 0; i < contour.size(); ++i) {
    auto const expected = sourceContour[i].p.rotatedClockwiseCopy(90.0f);
    requireNear(contour[i].p.x, expected.x,
                "rotated copy retained a source vertex x coordinate");
    requireNear(contour[i].p.y, expected.y,
                "rotated copy retained a source vertex y coordinate");
  }
}

}  // namespace

int main() {
  try {
    orientationInvalidatesExactBounds();
    rotatedCopyRefreshesTransformedVertices();
    std::cout << "Direct primitive mutations invalidate derived geometry\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
