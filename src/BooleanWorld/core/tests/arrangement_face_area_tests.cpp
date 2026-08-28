#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;
using bw::core::arr::FaceArea;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon, message);
}

Contour square(double min, double max) {
  auto fp = [](double v) { return bw::core::arr::ToFixedPointCoordinate(v); };
  return {{fp(min), fp(min)}, {fp(max), fp(min)}, {fp(max), fp(max)}, {fp(min), fp(max)}};
}

ArrangementPrimitive primitive(
    Contour contour, Primitive::Operation operation, uint8_t priority,
    uint32_t primitiveIndex) {
  return {{std::move(contour)}, operation,
          Primitive::FillRule::NonZero, priority, primitiveIndex};
}

void computesASimpleConvexFaceArea() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(square(0, 100), Primitive::Operation::Union, 0, 1)});

  require(arrangement->faces.size() == 2,
          "a single square should produce one bounded face");
  requireNear(FaceArea(arrangement->faces[1], *arrangement), 10000.0,
              "a convex square's face area should be its side length squared");
  requireNear(FaceArea(arrangement->faces[0], *arrangement), 0.0,
              "the unbounded exterior face should report zero area");
}

void subtractsOneHoleFromItsOuterBoundary() {
  auto base = primitive(square(0, 100), Primitive::Operation::Union, 0, 1);
  auto cut = primitive(square(25, 75), Primitive::Operation::Difference, 1, 2);

  auto arrangement = bw::core::arr::BuildArrangement({base, cut});
  require(arrangement->faces.size() == 3,
          "cutting a centred square hole should produce a ring face and its hole face");
  requireNear(FaceArea(arrangement->faces[1], *arrangement), 7500.0,
              "the ringed face's area did not subtract its hole");
}

void subtractsNestedHolesAndIslands() {
  // A 100x100 room, a 50x50 void cut from its centre, and a 20x20 island
  // re-filled at the centre of that void.
  auto room = primitive(square(0, 100), Primitive::Operation::Union, 0, 1);
  auto voidCut = primitive(square(25, 75), Primitive::Operation::Difference, 1, 2);
  auto island = primitive(square(40, 60), Primitive::Operation::Union, 2, 3);

  auto arrangement = bw::core::arr::BuildArrangement({room, voidCut, island});
  require(arrangement->faces.size() == 4,
          "a room, its void, and a re-filled island should produce three cycle faces");

  double totalSolidArea = 0.0;
  for (auto const& face : arrangement->faces) {
    if (face.solid) {
      totalSolidArea += FaceArea(face, *arrangement);
    }
  }
  requireNear(totalSolidArea, 7500.0 + 400.0,
              "nested hole and island areas were not combined correctly");
}

}  // namespace

int main() {
  try {
    computesASimpleConvexFaceArea();
    subtractsOneHoleFromItsOuterBoundary();
    subtractsNestedHolesAndIslands();
    std::cout << "Arrangement faces expose a hole-aware area\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
