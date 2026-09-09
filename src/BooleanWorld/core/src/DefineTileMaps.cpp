#include "core/DefineTileMaps.h"

#include <utility>

#include "core/CoreException.h"
#include "core/Serializer.h"

namespace bw::core {
using namespace std;

DefineTileMaps::DefineTileMaps() : DefineTileMaps(false) {}

DefineTileMaps::DefineTileMaps(bool deserializeLegacyTileMap)
    : mDeserializeLegacyTileMap(deserializeLegacyTileMap) {
  mTileMaps.emplace_back(new TileMap(this, 0, mMapSize, mCellSize));
}

DefineTileMaps* DefineTileMaps::instantiateLegacyTileMap() {
  return new DefineTileMaps(true);
}

bool DefineTileMaps::isMapSize(uint32_t size) {
  return size == 64 || size == 128 || size == 256;
}

bool DefineTileMaps::isCellSize(uint32_t size) {
  return size == 2 || size == 4 || size == 8 || size == 16 || size == 32;
}

string DefineTileMaps::getType() const { return "DefineTileMaps"; }
bool DefineTileMaps::mayBeFirstStep() const { return false; }

LayerBuildStep* DefineTileMaps::copy(
    map<VertexTransformerObject const*, VertexTransformerObject*>&) const {
  auto* result = new DefineTileMaps;
  result->copyFrom(*this);
  result->mMapSize = mMapSize;
  result->mCellSize = mCellSize;
  result->mTileMaps.clear();
  for (auto const& source : mTileMaps) {
    auto map = unique_ptr<TileMap>(new TileMap(
        result, source->mIndex, mMapSize, mCellSize));
    map->mCells = source->mCells;
    result->mTileMaps.push_back(move(map));
  }
  return result;
}

void DefineTileMaps::execute(LayerBuildContext&) const {}
bool DefineTileMaps::primitivesParticipateInBuild() const { return false; }
bool DefineTileMaps::permitsDirectPrimitiveEditing() const { return false; }
bool DefineTileMaps::acceptsNewPrimitives() const { return false; }
uint32_t DefineTileMaps::adoptPrimitive(Primitive*) {
  throw CoreException("DefineTileMaps does not accept Primitives");
}
void DefineTileMaps::replacePrimitive(Primitive*, Primitive*) {
  throw CoreException("DefineTileMaps has no Primitive output to edit");
}
void DefineTileMaps::releasePrimitive(Primitive*) {
  throw CoreException("DefineTileMaps has no Primitive output to move");
}
bool DefineTileMaps::ownsPrimitive(Primitive const*) const { return false; }

void DefineTileMaps::resetTileMaps() {
  for (uint32_t i = 0; i < mTileMaps.size(); ++i) {
    mTileMaps[i]->reset(i, mMapSize, mCellSize);
  }
}

void DefineTileMaps::tileMapModified() { modify(); }

void DefineTileMaps::setMapSize(uint32_t size) {
  if (!isMapSize(size)) {
    throw CoreException("DefineTileMaps Map size must be 64, 128, or 256");
  }
  if (mMapSize == size) return;
  mMapSize = size;
  resetTileMaps();
  modify();
}

void DefineTileMaps::setCellSize(uint32_t size) {
  if (!isCellSize(size)) {
    throw CoreException(
        "DefineTileMaps cell size must be 2, 4, 8, 16, or 32");
  }
  if (mCellSize == size) return;
  mCellSize = size;
  resetTileMaps();
  modify();
}

uint32_t DefineTileMaps::getMapSize() const { return mMapSize; }
uint32_t DefineTileMaps::getCellSize() const { return mCellSize; }

void DefineTileMaps::setNumTileMaps(uint32_t count) {
  if (count == 0 || count > MaxTileMaps) {
    throw CoreException("DefineTileMaps count must be between 1 and 16");
  }
  if (count == mTileMaps.size()) return;
  while (mTileMaps.size() > count) mTileMaps.pop_back();
  while (mTileMaps.size() < count) {
    auto const index = static_cast<uint32_t>(mTileMaps.size());
    mTileMaps.emplace_back(new TileMap(this, index, mMapSize, mCellSize));
  }
  modify();
}

uint32_t DefineTileMaps::getNumTileMaps() const {
  return static_cast<uint32_t>(mTileMaps.size());
}

TileMap* DefineTileMaps::getTileMap(uint32_t index) {
  if (index >= mTileMaps.size()) {
    throw CoreException("TileMap index is out of range");
  }
  return mTileMaps[index].get();
}

TileMap const* DefineTileMaps::getTileMap(uint32_t index) const {
  if (index >= mTileMaps.size()) {
    throw CoreException("TileMap index is out of range");
  }
  return mTileMaps[index].get();
}

void DefineTileMaps::serializeArgs(
    shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeUint32("mapSize", mMapSize);
  serializer->writeUint32("cellSize", mCellSize);
  serializer->beginArray("tileMaps");
  for (auto const& map : mTileMaps) {
    serializer->beginMap("tileMap");
    serializer->beginArray("cells", false);
    for (auto cell : map->mCells) serializer->writeUint8("value", cell);
    serializer->endArray();
    serializer->endMap();
  }
  serializer->endArray();
}

bool DefineTileMaps::deserializeArgs(
    shared_ptr<Serializer> serializer, SerializationWorkData&) {
  auto const mapSize = serializer->readUint32("mapSize");
  auto const cellSize = serializer->readUint32("cellSize");
  if (!isMapSize(mapSize)) {
    throw CoreException("DefineTileMaps Map size must be 64, 128, or 256");
  }
  if (!isCellSize(cellSize)) {
    throw CoreException(
        "DefineTileMaps cell size must be 2, 4, 8, 16, or 32");
  }

  auto const dimension = mapSize / cellSize;
  auto const expected = static_cast<size_t>(dimension) * dimension;
  vector<unique_ptr<TileMap>> maps;
  serializer->beginArray(mDeserializeLegacyTileMap ? "cells" : "tileMaps");
  do {
    if (!mDeserializeLegacyTileMap && !serializer->nextArrayItem()) break;
    if (maps.size() == MaxTileMaps) {
      throw CoreException("DefineTileMaps cannot contain more than 16 TileMaps");
    }
    if (!mDeserializeLegacyTileMap) serializer->beginMap("tileMap");
    auto map = unique_ptr<TileMap>(new TileMap(
        this, static_cast<uint32_t>(maps.size()), mapSize, cellSize));
    vector<uint8_t> cells;
    cells.reserve(expected);
    if (!mDeserializeLegacyTileMap) serializer->beginArray("cells");
    while (serializer->nextArrayItem()) {
      auto const value = serializer->readUint8();
      if (value > 1) throw CoreException("TileMap cell value must be 0 or 1");
      cells.push_back(value);
    }
    if (!mDeserializeLegacyTileMap) {
      serializer->endArray();
      serializer->endMap();
    }
    if (cells.size() != expected) {
      throw CoreException("TileMap cell count does not match its sizes");
    }
    map->mCells = move(cells);
    maps.push_back(move(map));
  } while (!mDeserializeLegacyTileMap);
  serializer->endArray();
  if (maps.empty()) {
    throw CoreException("DefineTileMaps must contain at least one TileMap");
  }

  mMapSize = mapSize;
  mCellSize = cellSize;
  mTileMaps = move(maps);
  mDeserializeLegacyTileMap = false;
  return true;
}

}  // namespace bw::core
