#include "core/TileMap.h"

#include <algorithm>

#include "core/CoreException.h"
#include "core/Serializer.h"

namespace bw::core {
using namespace std;

TileMap::TileMap() { resetCells(); }

bool TileMap::isMapSize(uint32_t size) {
  return size == 64 || size == 128 || size == 256;
}

bool TileMap::isCellSize(uint32_t size) {
  return size == 2 || size == 4 || size == 8 || size == 16 || size == 32;
}

string TileMap::getType() const { return "TileMap"; }
bool TileMap::mayBeFirstStep() const { return false; }

LayerBuildStep* TileMap::copy(
    map<VertexTransformerObject const*, VertexTransformerObject*>&) const {
  auto* result = new TileMap;
  result->copyFrom(*this);
  result->mMapSize = mMapSize;
  result->mCellSize = mCellSize;
  result->mCells = mCells;
  return result;
}

void TileMap::execute(LayerBuildContext&) const {}
bool TileMap::primitivesParticipateInBuild() const { return false; }
bool TileMap::permitsDirectPrimitiveEditing() const { return false; }
bool TileMap::acceptsNewPrimitives() const { return false; }
uint32_t TileMap::adoptPrimitive(Primitive*) {
  throw CoreException("TileMap does not accept Primitives");
}
void TileMap::replacePrimitive(Primitive*, Primitive*) {
  throw CoreException("TileMap has no Primitive output to edit");
}
void TileMap::releasePrimitive(Primitive*) {
  throw CoreException("TileMap has no Primitive output to move");
}
bool TileMap::ownsPrimitive(Primitive const*) const { return false; }

void TileMap::resetCells() {
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

void TileMap::setMapSize(uint32_t size) {
  if (!isMapSize(size)) {
    throw CoreException("TileMap Map size must be 64, 128, or 256");
  }
  if (mMapSize == size) return;
  mMapSize = size;
  resetCells();
  modify();
}

void TileMap::setCellSize(uint32_t size) {
  if (!isCellSize(size)) {
    throw CoreException("TileMap cell size must be 2, 4, 8, 16, or 32");
  }
  if (mCellSize == size) return;
  mCellSize = size;
  resetCells();
  modify();
}

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
  modify();
}

void TileMap::toggleCell(uint32_t x, uint32_t y) {
  auto& cell = mCells[cellIndex(x, y)];
  cell = cell ? 0 : 1;
  modify();
}

void TileMap::serializeArgs(
    shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeUint32("mapSize", mMapSize);
  serializer->writeUint32("cellSize", mCellSize);
  serializer->beginArray("cells", false);
  for (auto cell : mCells) serializer->writeUint8("value", cell);
  serializer->endArray();
}

bool TileMap::deserializeArgs(
    shared_ptr<Serializer> serializer, SerializationWorkData&) {
  auto const mapSize = serializer->readUint32("mapSize");
  auto const cellSize = serializer->readUint32("cellSize");
  if (!isMapSize(mapSize)) {
    throw CoreException("TileMap Map size must be 64, 128, or 256");
  }
  if (!isCellSize(cellSize)) {
    throw CoreException("TileMap cell size must be 2, 4, 8, 16, or 32");
  }

  auto const dimension = mapSize / cellSize;
  auto const expected = static_cast<size_t>(dimension) * dimension;
  vector<uint8_t> cells;
  cells.reserve(expected);
  serializer->beginArray("cells");
  while (serializer->nextArrayItem()) {
    auto const value = serializer->readUint8();
    if (value > 1) throw CoreException("TileMap cell value must be 0 or 1");
    cells.push_back(value);
  }
  serializer->endArray();
  if (cells.size() != expected) {
    throw CoreException("TileMap cell count does not match its sizes");
  }

  mMapSize = mapSize;
  mCellSize = cellSize;
  mCells = move(cells);
  return true;
}

}  // namespace bw::core
