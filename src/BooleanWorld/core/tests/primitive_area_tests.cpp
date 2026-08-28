#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/MeshPrimitive.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
#include <core/TorusPolygon.h>

namespace {

constexpr double Epsilon = 0.01;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon, message);
}


void computesASimpleConvexPrimitivesArea() {
  bw::core::RectanglePolygon rectangle(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rectangle.setSize(10.0f, 10.0f);
  rectangle.updateVertexPositions();

  // An axis-aligned, hole-free square's raw area must equal its own exact
  // bounding box area, whatever the primitive's internal size scaling.
  wp::Vector2 minimum, maximum;
  rectangle.calculateExactBounds().getExtents(minimum, maximum);
  auto boundsArea = double(maximum.x - minimum.x) * double(maximum.y - minimum.y);

  requireNear(rectangle.getArea(), boundsArea,
              "a scaled square primitive's raw area was wrong");
}

void subtractsAPrimitivesOwnHole() {
  constexpr float thickness = 0.5f;
  constexpr float size = 10.0f;

  bw::core::TorusPolygon torus(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      thickness,
      1.0f);
  torus.setSize(size, size);
  torus.updateVertexPositions();

  // TorusPolygon's outer and inner rings are generated with the exact same
  // per-vertex formula as RegularPolygon at the matching size, so two
  // independently constructed RegularPolygons - one at the torus's outer
  // size, one at its inner size - give a cross-check for the hole
  // subtraction that does not depend on the primitive's internal scaling.
  constexpr uint32_t numSides = 64;  // resolution 1.0 at TorusPolygon::BaseResolution
  bw::core::RegularPolygon outerDisk(
      bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero,
      numSides);
  outerDisk.setSize(size, size);
  outerDisk.updateVertexPositions();

  bw::core::RegularPolygon innerDisk(
      bw::core::Primitive::Operation::Union, bw::core::Primitive::FillRule::NonZero,
      numSides);
  innerDisk.setSize(size * (1.0f - thickness), size * (1.0f - thickness));
  innerDisk.updateVertexPositions();

  requireNear(torus.getArea(), outerDisk.getArea() - innerDisk.getArea(),
              "a torus primitive's own hole was not subtracted from its raw area");
}

void subtractsNestedHolesAndIslands() {
  bw::core::ClosedPolygon shellRing{
      {{0.0f, 0.0f}}, {{100.0f, 0.0f}}, {{100.0f, 100.0f}}, {{0.0f, 100.0f}}};
  bw::core::ClosedPolygon holeRing{
      {{25.0f, 25.0f}}, {{75.0f, 25.0f}}, {{75.0f, 75.0f}}, {{25.0f, 75.0f}}};
  bw::core::ClosedPolygon islandRing{
      {{40.0f, 40.0f}}, {{60.0f, 40.0f}}, {{60.0f, 60.0f}}, {{40.0f, 60.0f}}};

  bw::core::MeshFilledRegion island{islandRing, {}};
  bw::core::MeshHole hole{holeRing, {island}};
  bw::core::MeshFilledRegion shell{shellRing, {hole}};

  auto mesh = std::unique_ptr<bw::core::MeshPrimitive>(
      bw::core::MeshPrimitive::fromTree(
          bw::core::Primitive::Operation::Union, {shell}));

  requireNear(mesh->getArea(), (100.0 * 100.0 - 50.0 * 50.0) + 20.0 * 20.0,
              "a nested hole and island did not both contribute correctly");
}

}  // namespace

int main() {
  try {
    computesASimpleConvexPrimitivesArea();
    subtractsAPrimitivesOwnHole();
    subtractsNestedHolesAndIslands();
    std::cout << "Primitives expose their own raw, hole-aware area\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
