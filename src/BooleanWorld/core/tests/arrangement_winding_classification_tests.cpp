#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::ArrangementResult;
using bw::core::arr::Contour;
using bw::core::arr::FixedPointVertex;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Contour square(int64_t min, int64_t max) {
  return {{min, min}, {max, min}, {max, max}, {min, max}};
}

ArrangementPrimitive primitive(
    std::vector<Contour> contours,
    Primitive::FillRule fillRule,
    uint8_t priority,
    uint32_t primitiveIndex) {
  return {std::move(contours),
          Primitive::Operation::Union,
          fillRule,
          priority,
          primitiveIndex};
}

bw::core::arr::ArrangementFace const& faceAt(
    ArrangementResult const& arrangement, FixedPointVertex point) {
  for (auto const& face : arrangement.faces) {
    if (bw::core::arr::PointInFace(point, face, arrangement)) {
      return face;
    }
  }
  throw std::runtime_error("no bounded face contains the test point");
}

void preservesSignedWindingsFillRulesAndDisconnectedContours() {
  auto nonZeroOuter = square(0, 100);
  auto nonZeroHole = square(40, 60);
  std::reverse(nonZeroHole.begin(), nonZeroHole.end());

  auto arrangement = bw::core::arr::BuildArrangement(
      {primitive({nonZeroOuter, nonZeroHole},
                 Primitive::FillRule::NonZero, 1, 101),
       primitive({square(150, 250), square(190, 210)},
                 Primitive::FillRule::EvenOdd, 2, 102),
       primitive({square(300, 340), square(400, 440)},
                 Primitive::FillRule::NonZero, 3, 103)});

  auto const& nonZeroShell = faceAt(*arrangement, {10, 10});
  auto const& nonZeroHoleFace = faceAt(*arrangement, {50, 50});
  require(nonZeroShell.membership.contains(0) && nonZeroShell.solid,
          "the non-zero shell lost membership");
  require(!nonZeroHoleFace.membership.contains(0) && !nonZeroHoleFace.solid,
          "opposite signed contours did not cancel under non-zero fill");

  auto const& evenOddShell = faceAt(*arrangement, {160, 160});
  auto const& evenOddHole = faceAt(*arrangement, {200, 200});
  require(evenOddShell.membership.contains(1) && evenOddShell.solid,
          "the even-odd shell lost membership");
  require(!evenOddHole.membership.contains(1) && !evenOddHole.solid,
          "two same-signed contours did not cancel under even-odd fill");

  for (auto point : {FixedPointVertex{310, 310},
                     FixedPointVertex{410, 410}}) {
    auto const& disconnectedFace = faceAt(*arrangement, point);
    require(disconnectedFace.membership.contains(2) &&
                disconnectedFace.solid &&
                disconnectedFace.primitiveIndex == 103,
            "a disconnected contour changed membership, solidity, or winner");
  }
}

}  // namespace

int main() {
  try {
    preservesSignedWindingsFillRulesAndDisconnectedContours();
    std::cout << "Sparse winding propagation preserves exact classification\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
