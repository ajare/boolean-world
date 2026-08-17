#include <iostream>
#include <set>
#include <stdexcept>

#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

using bw::core::Primitive;
using bw::core::RectanglePolygon;
using bw::core::World;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

RectanglePolygon* addRectangle(World& world, wp::Vector2 const& position, float size = 2.0f) {
  auto rectangle = new RectanglePolygon(
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      1.0f);
  rectangle->setPosition(position);
  rectangle->setSize(size, size);
  world.addPrimitive(rectangle);
  return rectangle;
}

void exactPickingUsesGridCandidatesAndPreservesIndexOrder() {
  World world(512.0f, 4.0f);
  for (int i = 0; i < 128; ++i) {
    addRectangle(world, {-240.0f + float(i % 16) * 30.0f, -200.0f + float(i / 16) * 30.0f});
  }

  auto first = addRectangle(world, {100.0f, 100.0f}, 10.0f);
  auto second = addRectangle(world, {100.0f, 100.0f}, 10.0f);
  auto const firstIndex = first->getId();
  auto const secondIndex = second->getId();

  require(world.findPrimitiveIndex({100.0f, 100.0f}, true) == firstIndex,
          "exact picking did not preserve the first-index result");
  require(world.findPrimitiveIndex({100.0f, 100.0f}, true, {firstIndex}) == secondIndex,
          "exact picking did not apply the ignored-index set");

  auto indices = world.findPrimitiveIndices({100.0f, 100.0f}, true);
  require(indices == std::vector<uint32_t>({firstIndex, secondIndex}),
          "exact multi-picking did not preserve sorted candidate indices");
}

void cachedPickingTriangulationIsInvalidatedByGeometryChanges() {
  World world(100.0f, 5.0f);
  auto rectangle = addRectangle(world, {0.0f, 0.0f}, 10.0f);

  require(world.findPrimitiveIndex({0.0f, 5.0f}, true) == rectangle->getId(),
          "initial exact pick missed the rectangle");
  rectangle->setXyRatio(10.0f);
  require(world.findPrimitiveIndex({0.0f, 5.0f}, true) == ~0u,
          "exact picking reused a triangulation after geometry changed");
}

void pickingOutsideTheGridPreservesExistingBehavior() {
  World world(100.0f, 10.0f);
  auto rectangle = addRectangle(world, {49.0f, 0.0f}, 20.0f);

  require(world.findPrimitiveIndex({55.0f, 0.0f}, true) == rectangle->getId(),
          "exact picking outside world extents missed an overlapping primitive");
}

}  // namespace

int main() {
  try {
    exactPickingUsesGridCandidatesAndPreservesIndexOrder();
    cachedPickingTriangulationIsInvalidatedByGeometryChanges();
    pickingOutsideTheGridPreservesExistingBehavior();
    std::cout << "Exact primitive picking uses indexed, invalidation-safe triangulations\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
