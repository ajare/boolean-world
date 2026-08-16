#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <core/MeshPrimitive.h>
#include <core/World.h>

namespace {

using bw::core::ComplexPolygon;
using bw::core::MeshPrimitive;
using bw::core::Primitive;

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ComplexPolygon rectangle(float left, float bottom, float right, float top) {
  return {{{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}}};
}

MeshPrimitive* makeRectangle(
    Primitive::Operation operation, float left, float bottom, float right,
    float top) {
  return MeshPrimitive::fromComplexPolygons(
      operation, Primitive::FillRule::NonZero,
      {rectangle(left, bottom, right, top)});
}

void preservesListOrderForEqualPriorities() {
  bw::core::World world(20.0f, 2.0f);
  auto first = makeRectangle(Primitive::Operation::Union, 0.0f, 0.0f, 10.0f, 10.0f);
  auto second = makeRectangle(Primitive::Operation::Difference, 3.0f, 0.0f, 7.0f, 10.0f);
  auto third = makeRectangle(Primitive::Operation::Union, 4.0f, 0.0f, 6.0f, 10.0f);
  first->setPriority(4);
  second->setPriority(4);
  third->setPriority(4);
  world.addPrimitive(first);
  world.addPrimitive(second);
  world.addPrimitive(third);

  auto byPriority = world.getPrimitivesByPriority();
  require(byPriority == std::vector<Primitive*>{first, second, third},
          "equal-priority primitives did not retain their list order");

  std::unique_ptr<Primitive> baked(world.createMeshPrimitive({0, 1, 2}));
  require(baked != nullptr,
          "baking an equal-priority fold unexpectedly produced no primitive");
  baked->updateVertexPositions();
  auto triangulation = baked->triangulate(true);
  require(triangulation.pointInside({1.0f, 5.0f}),
          "the first union primitive was missing from the baked fold");
  require(!triangulation.pointInside({3.5f, 5.0f}),
          "the difference primitive did not follow the first union primitive");
  require(triangulation.pointInside({5.0f, 5.0f}),
          "the final union primitive did not follow the difference primitive");
}

}  // namespace

int main() {
  try {
    preservesListOrderForEqualPriorities();
    std::cout << "Equal-priority folds retain list order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
