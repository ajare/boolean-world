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

void classifiesAnOuterFaceWhenAHoleCoversEveryEarCentroid() {
  Contour outer{{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
  Contour hole{{10, 10}, {10, 990}, {990, 990}, {990, 10}};
  ArrangementPrimitive primitive{
      {std::move(outer), std::move(hole)},
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      0,
      23};

  auto arrangement = bw::core::arr::BuildArrangement({primitive});
  auto solidFaces = size_t{0};
  for (auto const& face : arrangement->faces) {
    if (face.solid) {
      ++solidFaces;
      require(face.membership.contains(0),
              "the narrow outer face lost its Primitive membership");
    }
  }
  require(solidFaces == 1,
          "the Primitive with a large hole should retain one solid outer face");
}

}  // namespace

int main() {
  try {
    classifiesSnappedSliverFromAnInteriorSample();
    classifiesAnOuterFaceWhenAHoleCoversEveryEarCentroid();
    std::cout << "Arrangement face samples remain inside bounded faces\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
