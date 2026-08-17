#include <algorithm>
#include <format>
#include <stdexcept>

#include "willpower/common/AccelerationGrid.h"

using namespace std;

namespace WP_NAMESPACE {

AccelerationGrid::AccelerationGrid(Vector2 const& offset, Vector2 const& size, int cellDimX, int cellDimY, float padding)
    : mOffset(offset), mSize(size), mCellDimX(cellDimX), mCellDimY(cellDimY), mMoveCount(0) {
  int cellCount = mCellDimX * mCellDimY;
  mCells.resize(cellCount);

  // Extend the grid a little to avoid floating point issues
  Vector2 extend = mSize * padding;
  mOffset -= extend;
  mSize += extend * 2;
}

AccelerationGrid::AccelerationGrid(float x, float y, float sizeX, float sizeY, int cellDimX, int cellDimY, float padding)
    : AccelerationGrid(Vector2(x, y), Vector2(sizeX, sizeY), cellDimX, cellDimY, padding) {
}

Vector2 const& AccelerationGrid::getOffset() const {
  return mOffset;
}

Vector2 const& AccelerationGrid::getSize() const {
  return mSize;
}

int AccelerationGrid::getCellDimensionX() const {
  return mCellDimX;
}

int AccelerationGrid::getCellDimensionY() const {
  return mCellDimY;
}

Vector2 AccelerationGrid::getCellSize() const {
  return Vector2(mSize.x / mCellDimX, mSize.y / mCellDimY);
}

AccelerationGrid::IndexCollection& AccelerationGrid::getCellItems(int x, int y) {
  return mCells[y * mCellDimX + x];
}

AccelerationGrid::IndexCollection const& AccelerationGrid::_getCellItems(int x, int y) const {
  return mCells[y * mCellDimX + x];
}

void AccelerationGrid::clear() {
  mIndicesToCells.clear();
  mMoveCount = 0;

  for (auto& cell : mCells) {
    cell.clear();
  }
}

void AccelerationGrid::addItemToCell(IndexCollection& cell, uint32_t index) {
  auto const position = lower_bound(cell.begin(), cell.end(), index);
  if (position == cell.end() || *position != index) {
    cell.insert(position, index);
  }
}

void AccelerationGrid::removeItemFromCell(IndexCollection& cell, uint32_t index) {
  auto const position = lower_bound(cell.begin(), cell.end(), index);
  if (position != cell.end() && *position == index) {
    cell.erase(position);
  }
}

void AccelerationGrid::addItem(uint32_t index, BoundingBox const& box) {
  Vector2 minExtent, maxExtent;
  box.getExtents(minExtent, maxExtent);

  Vector2 cellSize = getCellSize();
  Vector2 minCell = (minExtent - mOffset) / cellSize;
  Vector2 maxCell = (maxExtent - mOffset) / cellSize;

  int cellX0 = (int)minCell.x;
  int cellY0 = (int)minCell.y;
  int cellX1 = (int)maxCell.x;
  int cellY1 = (int)maxCell.y;

  cellX0 = max(0, min(cellX0, mCellDimX - 1));
  cellY0 = max(0, min(cellY0, mCellDimY - 1));
  cellX1 = max(0, min(cellX1, mCellDimX - 1));
  cellY1 = max(0, min(cellY1, mCellDimY - 1));

  if (mIndicesToCells.find(index) != mIndicesToCells.end()) {
    removeItem(index);
  }
  auto& indexToCells = mIndicesToCells[index];

  for (int y = cellY0; y <= cellY1; ++y) {
    for (int x = cellX0; x <= cellX1; ++x) {
      addItemToCell(getCellItems(x, y), index);
      indexToCells.push_back(uint32_t(mCellDimX * y + x));
    }
  }
}

void AccelerationGrid::removeItem(uint32_t index, bool failIfNotFound) {
  auto it = mIndicesToCells.find(index);
  if (it == mIndicesToCells.end()) {
    if (failIfNotFound) {
      throw runtime_error(format("Index {} not found in AccelerationGrid", index));
    }
    return;
  }

  for (auto cellIndex : it->second) {
    removeItemFromCell(mCells[cellIndex], index);
  }

  mIndicesToCells.erase(it);
}

void AccelerationGrid::removeAllItems() {
  for (auto& cell : mCells) {
    cell.clear();
  }

  mIndicesToCells.clear();
  mMoveCount = 0;
}

void AccelerationGrid::moveItem(uint32_t index, BoundingBox const& newBox) {
  removeItem(index);
  addItem(index, newBox);
  mMoveCount++;
}

int AccelerationGrid::getMoveCount() const {
  return mMoveCount;
}

void AccelerationGrid::resetMoveCount() {
  mMoveCount = 0;
}

int AccelerationGrid::getCount(int cellX, int cellY) const {
  return (int)_getCellItems(cellX, cellY).size();
}

void AccelerationGrid::getContainingCell(bool checkBounds, float x, float y, int& cellX, int& cellY) const {
  Vector2 cellSize = getCellSize();

  float dx = x - mOffset.x;
  float dy = y - mOffset.y;

  cellX = (int)(dx / cellSize.x);
  cellY = (int)(dy / cellSize.y);

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

void AccelerationGrid::getCellExtents(int cellX, int cellY, Vector2& minExtent, Vector2& maxExtent) {
  Vector2 cellSize = getCellSize();

  minExtent.x = mOffset.x + cellSize.x * cellX;
  minExtent.y = mOffset.y + cellSize.y * cellY;
  maxExtent = minExtent + cellSize;
}

void AccelerationGrid::getItemsInArea(Vector2 const& minExtent, Vector2 const& maxExtent, IndexCollection& indices) const {
  Vector2 cellSize = getCellSize();

  int minX = (std::max)(0, (int)((minExtent.x - mOffset.x) / cellSize.x));
  int minY = (std::max)(0, (int)((minExtent.y - mOffset.y) / cellSize.y));
  int maxX = (std::min)((int)((maxExtent.x - mOffset.x) / cellSize.x), mCellDimX - 1);
  int maxY = (std::min)((int)((maxExtent.y - mOffset.y) / cellSize.y), mCellDimY - 1);

  _getItemsInCellRange(minX, minY, maxX, maxY, indices);
}

void AccelerationGrid::_getItemsInCellRange(int x0, int y0, int x1, int y1, IndexCollection& indices) const {
  indices.clear();

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      auto const& cell = _getCellItems(x, y);
      indices.insert(indices.end(), cell.begin(), cell.end());
    }
  }

  sort(indices.begin(), indices.end());
  indices.erase(unique(indices.begin(), indices.end()), indices.end());
}

AccelerationGrid::IndexCollection const& AccelerationGrid::_getItemCellIndices(uint32_t index) const {
  return mIndicesToCells.at(index);
}

}  // namespace WP_NAMESPACE
