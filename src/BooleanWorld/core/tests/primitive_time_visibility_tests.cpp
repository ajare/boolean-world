#include <cmath>
#include <iostream>
#include <stdexcept>

#include <core/Defines.h>
#include <core/RectanglePolygon.h>
#include <core/WorldUpdateData.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon makePrimitive(wp::Vector2 const& position, float size) {
  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  primitive.setPosition(position);
  primitive.setSize(size, size);
  primitive.setFlags(BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE);
  return primitive;
}

bw::core::WorldUpdateData const viewData{
    {0.0f, 0.0f},
    0.0f,
    1.0f,
    90.0f,
    100.0f,
    false,
    false,
    bw::core::SelectLayer(0)};

void primitiveContainedByViewDoesNotAdvanceTime() {
  auto primitive = makePrimitive({0.0f, 50.0f}, 10.0f);

  primitive.updateTime(1.0f, viewData);

  require(primitive.getTime() == 0.0,
          "a primitive contained by the view triangle advanced time");
}

void viewContainedByPrimitiveDoesNotAdvanceTime() {
  auto primitive = makePrimitive({0.0f, 50.0f}, 500.0f);

  primitive.updateTime(1.0f, viewData);

  require(primitive.getTime() == 0.0,
          "a view triangle contained by a primitive advanced time");
}

void primitiveOutsideViewStillAdvancesTime() {
  auto primitive = makePrimitive({400.0f, 50.0f}, 10.0f);

  primitive.updateTime(1.0f, viewData);

  require(std::abs(primitive.getTime() - 1.0) < 0.0001,
          "a primitive outside the view triangle did not advance time");
}

}  // namespace

int main() {
  try {
    primitiveContainedByViewDoesNotAdvanceTime();
    viewContainedByPrimitiveDoesNotAdvanceTime();
    primitiveOutsideViewStillAdvancesTime();
    std::cout << "Primitive time visibility containment regressions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
