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
      operation, Primitive::FillRule::NonZero,
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

void selectsLayersAndOrdersPrioritiesStably() {
  bw::core::World world(20.0f, 2.0f);
  auto base = makeRectangle(
      Primitive::Operation::Union, 0.0f, 0.0f, 10.0f, 10.0f);
  auto excluded = makeRectangle(
      Primitive::Operation::Difference, 4.0f, 0.0f, 6.0f, 10.0f);
  auto allLayers = makeRectangle(
      Primitive::Operation::Difference, 4.0f, 0.0f, 6.0f, 10.0f);
  auto restore = makeRectangle(
      Primitive::Operation::Union, 4.0f, 0.0f, 6.0f, 10.0f);
  base->setLayer(0);
  base->setPriority(3);
  excluded->setLayer(1);
  excluded->setPriority(6);
  allLayers->setLayer(BW_LAYER_ALL);
  allLayers->setPriority(5);
  restore->setLayer(2);
  restore->setPriority(5);
  world.addPrimitive(base);
  world.addPrimitive(excluded);
  world.addPrimitive(allLayers);
  world.addPrimitive(restore);

  bw::core::LayerSelection selection;
  selection.set(0);
  selection.set(2);
  auto selected = bw::core::selectAndOrderPrimitives(world, selection);
  require(selected == std::vector<Primitive*>{base, allLayers, restore},
          "selected primitives were not globally priority-ordered stably");

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(&world, selection);
  require(isSolidAt(*generator.getWorldData(), 5.0f, 5.0f),
          "the arrangement generator did not use the selected stable fold");
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
    selectsLayersAndOrdersPrioritiesStably();
    preservesListOrderForEqualPriorities();
    std::cout << "Layer selection and equal-priority folds retain order\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
