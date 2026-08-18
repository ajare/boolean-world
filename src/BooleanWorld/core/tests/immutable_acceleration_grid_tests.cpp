#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <willpower/common/BoundingBox.h>

#include <core/ImmutableAccelerationGrid.h>

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void bulkBuildsSortedDuplicateFreeCandidates() {
  using Grid = bw::core::ImmutableAccelerationGrid;
  std::vector<Grid::ItemBounds> bounds{
      {{0.25f, 0.25f}, {2.25f, 0.75f}},
      {{1.25f, 0.25f}, {1.75f, 1.75f}},
      {{0.50f, 0.50f}, {2.50f, 2.50f}},
      {{2.25f, 2.25f}, {2.75f, 2.75f}}};
  Grid grid({0.0f, 0.0f}, {3.0f, 3.0f}, 3, 3, bounds);

  auto centreCell = grid.getCellItems(1, 1);
  require(
      std::vector<uint32_t>(centreCell.begin(), centreCell.end()) ==
          std::vector<uint32_t>({1, 2}),
      "bulk-built cell candidates are not sorted and duplicate-free");

  Grid::IndexCollection candidates;
  grid.getCandidateItemsInBoundingArea(
      wp::BoundingBox(0.0f, 0.0f, 3.0f, 3.0f), candidates);
  require(candidates == std::vector<uint32_t>({0, 1, 2, 3}),
          "multi-cell candidates are not sorted and duplicate-free");
}

void containingCellRetainsBoundaryBehavior() {
  using Grid = bw::core::ImmutableAccelerationGrid;
  Grid grid(
      {0.0f, 0.0f}, {2.0f, 2.0f}, 2, 2,
      std::span<Grid::ItemBounds const>{});
  int x = 0;
  int y = 0;
  grid.getContainingCell(true, -0.1f, 0.5f, x, y);
  require(x == -1 && y == 0, "negative points must remain outside the grid");
  grid.getContainingCell(true, 2.0f, 2.0f, x, y);
  require(x == -1 && y == -1, "maximum extents must retain exclusive bounds");
}
}  // namespace

int main() {
  try {
    bulkBuildsSortedDuplicateFreeCandidates();
    containingCellRetainsBoundaryBehavior();
    std::cout << "Immutable acceleration-grid bulk construction passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
