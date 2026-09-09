#include "core/TileMap.h"

#include "core/CoreException.h"
#include "core/DefineTileMaps.h"

namespace bw::core {

TileMap::TileMap(DefineTileMaps* owner, uint32_t index, uint32_t mapSize,
                 uint32_t cellSize)
    : mOwner(owner) {
  reset(index, mapSize, cellSize);
}

void TileMap::reset(uint32_t index, uint32_t mapSize, uint32_t cellSize) {
  mIndex = index;
  mMapSize = mapSize;
  mCellSize = cellSize;
  auto const dimension = getWidth();
  mCells.assign(static_cast<size_t>(dimension) * dimension, uint8_t{0});
}

size_t TileMap::cellIndex(uint32_t x, uint32_t y) const {
  auto const dimension = getWidth();
  if (x >= dimension || y >= dimension) {
    throw CoreException("TileMap cell coordinate is out of range");
  }
  return static_cast<size_t>(y) * dimension + x;
}

uint32_t TileMap::getIndex() const { return mIndex; }
uint32_t TileMap::getMapSize() const { return mMapSize; }
uint32_t TileMap::getCellSize() const { return mCellSize; }
uint32_t TileMap::getWidth() const { return mMapSize / mCellSize; }
uint32_t TileMap::getHeight() const { return getWidth(); }

int TileMap::getCell(uint32_t x, uint32_t y) const {
  return static_cast<int>(mCells[cellIndex(x, y)]);
}

void TileMap::setCell(uint32_t x, uint32_t y, int value) {
  if (value != 0 && value != 1) {
    throw CoreException("TileMap cell value must be 0 or 1");
  }
  auto& cell = mCells[cellIndex(x, y)];
  if (cell == value) return;
  cell = static_cast<uint8_t>(value);
  mOwner->tileMapModified();
}

void TileMap::toggleCell(uint32_t x, uint32_t y) {
  auto& cell = mCells[cellIndex(x, y)];
  cell = cell ? 0 : 1;
  mOwner->tileMapModified();
}

}  // namespace bw::core
