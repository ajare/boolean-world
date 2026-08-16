#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void removingDetachedPrimitivePreservesLookupGrid() {
  bw::core::World world(100.0f, 10.0f);
  auto resident = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  resident->setSize(10.0f, 10.0f);
  world.addPrimitive(resident);

  auto detached = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  detached->setId(resident->getId());

  world.removePrimitive(detached, false);
  delete detached;

  require(world.getNumPrimitives() == 1,
          "removing a detached primitive changed world membership");
  auto candidates = world.findPrimitives(resident->getBounds());
  require(std::find(candidates.begin(), candidates.end(), resident) != candidates.end(),
          "removing a detached primitive removed a resident primitive from the lookup grid");
}

}  // namespace

int main() {
  try {
    removingDetachedPrimitivePreservesLookupGrid();
    std::cout << "Removing detached primitives preserves the lookup grid\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
