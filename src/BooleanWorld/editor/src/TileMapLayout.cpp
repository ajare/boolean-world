#include "TileMapLayout.h"

#include <cmath>

namespace editor {

wp::Vector2 tileMapOrigin(
    uint32_t mapIndex, uint32_t mapSize, uint32_t cellSize) {
  auto const pitch = static_cast<float>(mapSize + cellSize);
  return {
      static_cast<float>(mapIndex % 4) * pitch,
      static_cast<float>(mapIndex / 4) * pitch};
}

std::optional<TileMapCellLocation> tileMapCellAt(
    wp::Vector2 const& worldPosition, uint32_t mapCount,
    uint32_t mapSize, uint32_t cellSize) {
  if (worldPosition.x < 0.0f || worldPosition.y < 0.0f ||
      mapCount == 0 || cellSize == 0) {
    return std::nullopt;
  }

  auto const pitch = static_cast<float>(mapSize + cellSize);
  auto const column = static_cast<uint32_t>(std::floor(worldPosition.x / pitch));
  auto const row = static_cast<uint32_t>(std::floor(worldPosition.y / pitch));
  if (column >= 4) return std::nullopt;
  auto const mapIndex = row * 4 + column;
  if (mapIndex >= mapCount) return std::nullopt;

  auto const origin = tileMapOrigin(mapIndex, mapSize, cellSize);
  auto const local = worldPosition - origin;
  if (local.x >= mapSize || local.y >= mapSize) return std::nullopt;
  return TileMapCellLocation{
      mapIndex,
      static_cast<uint32_t>(std::floor(local.x / cellSize)),
      static_cast<uint32_t>(std::floor(local.y / cellSize))};
}

}  // namespace editor
