#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Contour;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Contour square() {
  return {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
}

ArrangementPrimitive primitive(
    Primitive::Operation operation, uint8_t priority,
    uint32_t primitiveIndex) {
  return {{square()},
          operation,
          Primitive::FillRule::NonZero,
          priority,
          primitiveIndex};
}

void assignsHighestPriorityMemberWhenNoRunWins() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(Primitive::Operation::Difference, 1, 41),
       primitive(Primitive::Operation::XOR, 2, 42)});

  require(arrangement->faces.size() == 2,
          "coincident primitives should produce one bounded face");
  auto const& face = arrangement->faces[1];
  require(face.solid,
          "difference followed by XOR should produce a solid member face");
  require(face.membership.contains(0) && face.membership.contains(1),
          "the solid face should retain both primitive memberships");
  require(face.paletteIndex == 2,
          "a solid face without a winning run should use its highest-priority member");
  require(face.primitiveIndex == 42,
          "the fallback winner should preserve the authored primitive index");
}

void preservesRunBasedWinnerWhenOneExists() {
  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive(Primitive::Operation::Union, 1, 51),
       primitive(Primitive::Operation::Intersection, 2, 52)});

  auto const& face = arrangement->faces[1];
  require(face.solid, "the union/intersection face should be solid");
  require(face.paletteIndex == 1 && face.primitiveIndex == 51,
          "the fallback must not replace an existing run-based winner");
}

}  // namespace

int main() {
  try {
    assignsHighestPriorityMemberWhenNoRunWins();
    preservesRunBasedWinnerWhenOneExists();
    std::cout << "Solid arrangement faces always have a winning primitive\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
