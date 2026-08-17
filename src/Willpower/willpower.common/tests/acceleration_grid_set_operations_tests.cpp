#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

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

  typename Grid::IndexCollection indices;
  grid._getItemsInCellRange(0, 0, 1, 0, indices);
  require(indices == std::vector<uint32_t>({1, 2, 3}),
          gridName + " did not return the union of both cells");

  auto const capacity = indices.capacity();
  grid._getItemsInCellRange(0, 0, 1, 0, indices);
  require(indices.capacity() == capacity,
          gridName + " did not reuse caller-owned query storage");
}

void accelerationGridCombinesItemsWithoutOverlappingRanges() {
  static_assert(std::is_same_v<wp::AccelerationGrid::IndexCollection, std::vector<uint32_t>>);
  requireCellRangeCombinesItemsWithoutOverlappingRanges<wp::AccelerationGrid>(
      "AccelerationGrid");
}

void extendedAccelerationGridCombinesItemsWithoutOverlappingRanges() {
  requireCellRangeCombinesItemsWithoutOverlappingRanges<wp::ExtendedAccelerationGrid<int>>(
      "ExtendedAccelerationGrid");
}

template <typename Grid>
void requireReplacingItemRemovesItFromPreviousCells(std::string const& gridName) {
  Grid grid(0.0f, 0.0f, 100.0f, 25.0f, 4, 1, 0.0f);
  wp::BoundingBox const oldBounds(5.0f, 5.0f, 5.0f, 5.0f);
  wp::BoundingBox const newBounds(80.0f, 5.0f, 5.0f, 5.0f);

  grid.addItem(1, oldBounds);
  grid.addItem(1, newBounds);

  typename Grid::IndexCollection candidates;
  grid.getCandidateItemsInBoundingArea(oldBounds, candidates);
  require(candidates.empty(), gridName + " retained a replaced item in its previous cells");
  grid.getCandidateItemsInBoundingArea(newBounds, candidates);
  require(candidates == std::vector<uint32_t>({1}),
          gridName + " did not retain a replaced item in its new cells");
}

void accelerationGridReplacesExistingItems() {
  requireReplacingItemRemovesItFromPreviousCells<wp::AccelerationGrid>("AccelerationGrid");
}

void extendedAccelerationGridReplacesExistingItems() {
  requireReplacingItemRemovesItFromPreviousCells<wp::ExtendedAccelerationGrid<int>>(
      "ExtendedAccelerationGrid");
}

void accelerationGridRejectsRemovingMissingItems() {
  wp::AccelerationGrid grid(0.0f, 0.0f, 100.0f, 25.0f, 4, 1, 0.0f);

  bool threw = false;
  try {
    grid.removeItem(1);
  } catch (std::runtime_error const&) {
    threw = true;
  }

  require(threw, "AccelerationGrid did not reject removing a missing item");
  grid.removeItem(1, false);
}

void extendedAccelerationGridRejectsMissingCellItemsWithoutUpdatingMetadata() {
  using Grid = wp::ExtendedAccelerationGrid<int>;

  Grid grid(0.0f, 0.0f, 100.0f, 25.0f, 4, 1, 0.0f);
  grid.addItem(1, wp::BoundingBox(5.0f, 5.0f, 5.0f, 5.0f));

  static_assert(std::is_same_v<Grid::IndexCollection, std::vector<uint32_t>>);

  // Simulate a stale index-to-cell record and verify its removal contract.
  auto& cell = const_cast<Grid::Cell&>(static_cast<Grid const&>(grid).getCell(0, 0));
  cell.indices.clear();

  int updateCount = 0;
  bool threw = false;
  try {
    grid.removeItem(1, [&updateCount](int*) { ++updateCount; });
  } catch (std::runtime_error const& error) {
    threw = std::string(error.what()) == "Index 1 not found in cell";
  }

  require(threw, "ExtendedAccelerationGrid did not reject a missing cell item");
  require(updateCount == 0, "ExtendedAccelerationGrid updated metadata for a missing cell item");

  grid.removeItem(1, [&updateCount](int*) { ++updateCount; }, false);
  require(updateCount == 0, "ExtendedAccelerationGrid updated metadata for a tolerated missing cell item");
}

}  // namespace

int main() {
  try {
    accelerationGridCombinesItemsWithoutOverlappingRanges();
    extendedAccelerationGridCombinesItemsWithoutOverlappingRanges();
    accelerationGridReplacesExistingItems();
    extendedAccelerationGridReplacesExistingItems();
    accelerationGridRejectsRemovingMissingItems();
    extendedAccelerationGridRejectsMissingCellItemsWithoutUpdatingMetadata();
    std::cout << "Acceleration-grid replacement and failure contracts passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
