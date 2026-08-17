#include <iostream>
#include <stdexcept>
#include <vector>

#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makePrimitive(float x) {
  auto primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive->setPosition({x, 0.0f});
  primitive->setSize(2.0f, 2.0f);
  return primitive;
}

void removesUnorderedPrimitiveIndices() {
  bw::core::World world(100.0f, 10.0f);
  world.addPrimitive(makePrimitive(-20.0f));
  world.addPrimitive(makePrimitive(-10.0f));
  world.addPrimitive(makePrimitive(0.0f));
  world.addPrimitive(makePrimitive(10.0f));
  world.addPrimitive(makePrimitive(20.0f));

  world.removePrimitives({3, 1});

  require(world.getNumPrimitives() == 3, "wrong number of primitives after removal");
  auto const& primitives = world.getPrimitives();
  for (uint32_t i = 0; i < uint32_t(primitives.size()); ++i) {
    require(primitives[i]->getId() == i, "remaining primitive has an incorrect id");
  }
  require(primitives[0]->getPosition().x == -20.0f, "first remaining primitive changed");
  require(primitives[1]->getPosition().x == 0.0f, "second remaining primitive changed");
  require(primitives[2]->getPosition().x == 20.0f, "third remaining primitive changed");
}

}  // namespace

int main() {
  try {
    removesUnorderedPrimitiveIndices();
    std::cout << "Removing unordered primitive indices preserves remaining primitives\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
