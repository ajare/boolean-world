#include "core/ImmutableAccelerationGrid.h"

#include <algorithm>
#include <stdexcept>

namespace bw::core {
namespace {
struct CellRange {
  int x0;
  int y0;
  int x1;
  int y1;
};

CellRange GetCellRange(
    ImmutableAccelerationGrid::ItemBounds const& bounds,
    wp::Vector2 const& offset,
    wp::Vector2 const& cellSize,
    int cellDimX,
    int cellDimY) {
  auto minCell = (bounds.minExtent - offset) / cellSize;
  auto maxCell = (bounds.maxExtent - offset) / cellSize;
  return {
      std::clamp(int(minCell.x), 0, cellDimX - 1),
      std::clamp(int(minCell.y), 0, cellDimY - 1),
      std::clamp(int(maxCell.x), 0, cellDimX - 1),
      std::clamp(int(maxCell.y), 0, cellDimY - 1)};
}
}  // namespace

ImmutableAccelerationGrid::ImmutableAccelerationGrid(
    wp::Vector2 const& offset,
    wp::Vector2 const& size,
    int cellDimX,
    int cellDimY,
    std::span<ItemBounds const> itemBounds)
    : mOffset(offset),
      mSize(size),
      mCellDimX(cellDimX),
      mCellDimY(cellDimY) {
  if (cellDimX <= 0 || cellDimY <= 0 || size.x <= 0.0f || size.y <= 0.0f) {
    throw std::invalid_argument("ImmutableAccelerationGrid dimensions must be positive");
  }

  auto const cellCount = size_t(cellDimX) * size_t(cellDimY);
  mCellOffsets.assign(cellCount + 1, 0);
  auto const cellSize = getCellSize();

  for (auto const& bounds : itemBounds) {
    auto range = GetCellRange(
        bounds, mOffset, cellSize, mCellDimX, mCellDimY);
    for (int y = range.y0; y <= range.y1; ++y) {
      for (int x = range.x0; x <= range.x1; ++x) {
        ++mCellOffsets[size_t(y) * size_t(mCellDimX) + size_t(x) + 1];
      }
    }
  }
  for (size_t cell = 1; cell < mCellOffsets.size(); ++cell) {
    mCellOffsets[cell] += mCellOffsets[cell - 1];
  }

  mItems.resize(mCellOffsets.back());
  auto writeOffsets = mCellOffsets;
  for (uint32_t itemIndex = 0; itemIndex < uint32_t(itemBounds.size());
       ++itemIndex) {
    auto range = GetCellRange(
        itemBounds[itemIndex], mOffset, cellSize, mCellDimX, mCellDimY);
    for (int y = range.y0; y <= range.y1; ++y) {
      for (int x = range.x0; x <= range.x1; ++x) {
        auto cell = size_t(y) * size_t(mCellDimX) + size_t(x);
        mItems[writeOffsets[cell]++] = itemIndex;
      }
    }
  }
}

wp::Vector2 ImmutableAccelerationGrid::getCellSize() const {
  return {mSize.x / float(mCellDimX), mSize.y / float(mCellDimY)};
}

void ImmutableAccelerationGrid::getContainingCell(
    bool checkBounds,
    float x,
    float y,
    int& cellX,
    int& cellY) const {
  auto cellSize = getCellSize();
  auto dx = x - mOffset.x;
  auto dy = y - mOffset.y;
  cellX = int(dx / cellSize.x);
  cellY = int(dy / cellSize.y);
  if (dx < 0.0f && cellX == 0) {
    cellX = -1;
  }
  if (dy < 0.0f && cellY == 0) {
    cellY = -1;
  }
  if (checkBounds) {
    if (cellX < 0 || cellX >= mCellDimX) {
      cellX = -1;
    }
    if (cellY < 0 || cellY >= mCellDimY) {
      cellY = -1;
    }
  }
}

std::span<uint32_t const> ImmutableAccelerationGrid::getCellItems(
    int x,
    int y) const {
  auto cell = size_t(y) * size_t(mCellDimX) + size_t(x);
  return std::span<uint32_t const>(mItems).subspan(
      mCellOffsets[cell], mCellOffsets[cell + 1] - mCellOffsets[cell]);
}

void ImmutableAccelerationGrid::getItemsInArea(
    wp::Vector2 const& minExtent,
    wp::Vector2 const& maxExtent,
    IndexCollection& indices) const {
  auto cellSize = getCellSize();
  auto minX = std::max(0, int((minExtent.x - mOffset.x) / cellSize.x));
  auto minY = std::max(0, int((minExtent.y - mOffset.y) / cellSize.y));
  auto maxX = std::min(
      int((maxExtent.x - mOffset.x) / cellSize.x), mCellDimX - 1);
  auto maxY = std::min(
      int((maxExtent.y - mOffset.y) / cellSize.y), mCellDimY - 1);

  indices.clear();
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      auto cell = getCellItems(x, y);
      indices.insert(indices.end(), cell.begin(), cell.end());
    }
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

size_t ImmutableAccelerationGrid::storageBytes() const {
  return mCellOffsets.capacity() * sizeof(uint32_t) +
         mItems.capacity() * sizeof(uint32_t);
}

}  // namespace bw::core
