#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/ExtendedAccelerationGrid.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Grid>
void requireCellRangeCombinesItemsWithoutOverlappingRanges(std::string const& gridName) {
  Grid grid(0.0f, 0.0f, 100.0f, 50.0f, 2, 1, 0.0f);
  grid.addItem(1, wp::BoundingBox(0.0f, 0.0f, 60.0f, 10.0f));
  grid.addItem(2, wp::BoundingBox(10.0f, 10.0f, 5.0f, 5.0f));
  grid.addItem(3, wp::BoundingBox(70.0f, 10.0f, 5.0f, 5.0f));

  auto const indices = grid._getItemsInCellRange(0, 0, 1, 0);
  require(indices == std::set<uint32_t>({1, 2, 3}),
          gridName + " did not return the union of both cells");
}

void accelerationGridCombinesItemsWithoutOverlappingRanges() {
  requireCellRangeCombinesItemsWithoutOverlappingRanges<wp::AccelerationGrid>(
      "AccelerationGrid");
}

void extendedAccelerationGridCombinesItemsWithoutOverlappingRanges() {
  requireCellRangeCombinesItemsWithoutOverlappingRanges<wp::ExtendedAccelerationGrid<int>>(
      "ExtendedAccelerationGrid");
}

}  // namespace

int main() {
  try {
    accelerationGridCombinesItemsWithoutOverlappingRanges();
    extendedAccelerationGridCombinesItemsWithoutOverlappingRanges();
    std::cout << "Acceleration-grid set operations passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
