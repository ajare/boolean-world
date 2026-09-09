#pragma once

#include <cstdint>
#include <optional>

#include <willpower/common/Vector2.h>

namespace editor {

struct TileMapCellLocation {
  uint32_t mapIndex;
  uint32_t x;
  uint32_t y;
};

[[nodiscard]] wp::Vector2 tileMapOrigin(
    uint32_t mapIndex, uint32_t mapSize, uint32_t cellSize);

[[nodiscard]] std::optional<TileMapCellLocation> tileMapCellAt(
    wp::Vector2 const& worldPosition, uint32_t mapCount,
    uint32_t mapSize, uint32_t cellSize);

}  // namespace editor
