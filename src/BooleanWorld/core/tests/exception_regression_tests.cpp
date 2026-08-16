#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/CoreException.h>
#include <core/Interpolator.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireRuntimeError(Function&& function, char const* message) {
  try {
    function();
  } catch (std::runtime_error const& error) {
    require(std::string(error.what()).contains("not found in AccelerationGrid"),
            "rethrow did not preserve the exception message");
    return;
  } catch (std::exception const&) {
    throw std::runtime_error(message);
  }
  throw std::runtime_error("expected an exception");
}

bw::core::RectanglePolygon* makeRectangle() {
  auto* rectangle = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
  rectangle->setSize(10.0f, 10.0f);
  return rectangle;
}

void worldReplacementPreservesExceptionType() {
  bw::core::World world(100.0f, 10.0f);
  auto* resident = makeRectangle();
  world.addPrimitive(resident);
  resident->setId(99);

  auto replacement = std::unique_ptr<bw::core::RectanglePolygon>(makeRectangle());
  requireRuntimeError(
      [&] { world.replacePrimitive(0, replacement.get()); },
      "replacePrimitive sliced the acceleration-grid exception");
}

void coreExceptionsArePortableAndDescriptive() {
  bw::core::Interpolator<float> interpolator;
  try {
    interpolator.setPoints({{1.0f, 1.0f}, {0.0f, 0.0f}});
  } catch (bw::core::CoreException const& error) {
    require(std::string(error.what()) == "Interpolator points not ascending in time",
            "CoreException did not preserve its message");
    return;
  }
  throw std::runtime_error("Interpolator did not throw CoreException");
}

}  // namespace

int main() {
  try {
    worldReplacementPreservesExceptionType();
    coreExceptionsArePortableAndDescriptive();
    std::cout << "Exception regression coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
