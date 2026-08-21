#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <core/ArrangementWorldDataGenerator.h>
#include <core/LayerSelection.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>
#include <core/WorldDataGenerator.h>

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
      operation,
      {rectangle(left, bottom, right, top)});
}

bool isSolidAt(
    bw::core::arr::ArrangementResult const& arrangement, float x, float y) {
  auto const point = bw::core::arr::FixedPointVertex{
      bw::core::arr::ToFixedPointCoordinate(x),
      bw::core::arr::ToFixedPointCoordinate(y)};
  for (auto const& face : arrangement.faces) {
    if (bw::core::arr::PointInFace(point, face, arrangement)) {
      return face.solid;
    }
  }
  return false;
}

void ordersPrioritiesStablyAcrossTheFold() {
  bw::core::World world(20.0f, 2.0f);
  auto base = makeRectangle(
      Primitive::Operation::Union, 0.0f, 0.0f, 10.0f, 10.0f);
  auto cut = makeRectangle(
      Primitive::Operation::Difference, 4.0f, 0.0f, 6.0f, 10.0f);
  auto restore = makeRectangle(
      Primitive::Operation::Union, 4.0f, 0.0f, 6.0f, 10.0f);
  // Authored out of priority order, so the fold has to reorder them, and the
  // two equal priorities have to keep their relative authoring order.
  restore->setPriority(5);
  base->setPriority(3);
  cut->setPriority(5);
  world.addPrimitive(restore);
  world.addPrimitive(base);
  world.addPrimitive(cut);

  auto const selection = bw::core::SelectLayer(0);
  auto selected = bw::core::selectAndOrderPrimitives(world, selection);
  require(selected == std::vector<Primitive*>{base, restore, cut},
          "folded primitives were not globally priority-ordered stably");

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(&world, selection);
  require(!isSolidAt(*generator.getWorldData(), 5.0f, 5.0f),
          "the arrangement generator did not use the stable priority fold");
}

void bakingPreservesArrangementContainmentWithoutInference() {
  bw::core::World world(40.0f, 2.0f);
  auto shell = makeRectangle(
      Primitive::Operation::Union, -10.0f, -10.0f, 10.0f, 10.0f);
  auto hole = makeRectangle(
      Primitive::Operation::Difference, -8.0f, -8.0f, 8.0f, 8.0f);
  auto island = makeRectangle(
      Primitive::Operation::Union, -6.0f, -6.0f, 6.0f, 6.0f);
  auto nestedHole = makeRectangle(
      Primitive::Operation::Difference, -4.0f, -4.0f, 4.0f, 4.0f);
  world.addPrimitive(shell);
  world.addPrimitive(hole);
  world.addPrimitive(island);
  world.addPrimitive(nestedHole);

  auto baked = std::unique_ptr<Primitive>(
      world.createMeshPrimitive({0, 1, 2, 3}));
  auto* mesh = dynamic_cast<MeshPrimitive*>(baked.get());
  require(mesh && mesh->getShells().size() == 1,
          "arrangement baking did not produce one tree-native Shell");
  auto const& bakedShell = mesh->getShells().front();
  require(bakedShell.holes.size() == 1 &&
              bakedShell.holes.front().islands.size() == 1 &&
              bakedShell.holes.front().islands.front().holes.size() == 1,
          "arrangement baking lost nested Hole/Island topology");
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
    ordersPrioritiesStablyAcrossTheFold();
    bakingPreservesArrangementContainmentWithoutInference();
    preservesListOrderForEqualPriorities();
    std::cout << "Priority folds retain stable order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
