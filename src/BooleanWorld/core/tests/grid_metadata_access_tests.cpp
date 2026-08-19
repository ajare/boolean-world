#include <iostream>
#include <stdexcept>
#include <type_traits>

#include <core/World.h>

namespace {

using GridCellFrameNumberAccessor = void (bw::core::World::*)(uint32_t, frame_number_type*) const;
using PrimitivesInGridCellAccessor = std::vector<bw::core::Primitive*> (bw::core::World::*)(uint32_t) const;

template <typename Type>
concept HasGridCellPrimitivesVersionAccessor = requires {
  &Type::getGridCellPrimitivesVersion;
};

static_assert(
    std::is_same_v<decltype(&bw::core::World::getGridCellFrameNumber), GridCellFrameNumberAccessor>,
    "grid metadata must have one frame-number accessor");
static_assert(
    !HasGridCellPrimitivesVersionAccessor<bw::core::World>,
    "grid metadata must not retain the duplicate primitives-version accessor");
static_assert(
    std::is_same_v<decltype(&bw::core::World::getPrimitivesInGridCell), PrimitivesInGridCellAccessor>,
    "grid-cell primitive lookup must not accept an unwritten metadata parameter "
    "or a per-primitive layer tag");

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void gridMetadataUsesTheFrameNumberAccessor() {
  bw::core::World world(100.0f, 10.0f);
  frame_number_type frameNumber{-1};

  world.getGridCellFrameNumber(0, &frameNumber);

  require(frameNumber == 0, "a new grid cell did not report its frame number");
  require(world.getPrimitivesInGridCell(0).empty(),
          "a new grid cell unexpectedly contained primitives");
}

}  // namespace

int main() {
  try {
    gridMetadataUsesTheFrameNumberAccessor();
    std::cout << "Grid metadata uses the frame-number accessor\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
