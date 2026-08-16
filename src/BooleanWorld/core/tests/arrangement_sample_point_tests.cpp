#include <cstdint>
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

ArrangementPrimitive snappedSliverPrimitive() {
  // The first edge's midpoint is less than one fixed-point quantum from the
  // opposite boundary. A fixed quarter-quantum normal offset leaves the face.
  Contour contour{{0, 0}, {1000, 19}, {421, 8}};
  return {{std::move(contour)},
          Primitive::Operation::Union,
          Primitive::FillRule::EvenOdd,
          0,
          17};
}

void classifiesSnappedSliverFromAnInteriorSample() {
  auto arrangement =
      bw::core::arr::BuildArrangement({snappedSliverPrimitive()});

  require(arrangement->faces.size() == 2,
          "the snapped sliver should produce one bounded face");
  require(arrangement->faces[1].solid,
          "the snapped sliver face should be classified as solid");
  require(arrangement->faces[1].membership.contains(0),
          "the snapped sliver sample should retain primitive membership");
  require(arrangement->faces[1].outerBoundary.size() == 3,
          "the snapped sliver boundary should remain intact");
}

}  // namespace

int main() {
  try {
    classifiesSnappedSliverFromAnInteriorSample();
    std::cout << "Arrangement face samples remain inside snapped slivers\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
