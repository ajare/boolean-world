#include <iostream>
#include <stdexcept>

#include "TileMapLayout.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

void mapsUseFourColumnsAndOneCellOfPadding() {
  require(editor::tileMapOrigin(0, 256, 32) == wp::Vector2{0.0f, 0.0f},
          "TileMap zero did not begin at the World origin");
  require(editor::tileMapOrigin(1, 256, 32) == wp::Vector2{288.0f, 0.0f},
          "TileMaps were not separated by one cell of padding");
  require(editor::tileMapOrigin(4, 256, 32) == wp::Vector2{0.0f, 288.0f},
          "TileMaps did not wrap after four columns");

  auto cell = editor::tileMapCellAt({321.0f, 65.0f}, 5, 256, 32);
  require(cell && cell->mapIndex == 1 && cell->x == 1 && cell->y == 2,
          "world position did not resolve to the expected TileMap cell");
  require(!editor::tileMapCellAt({270.0f, 65.0f}, 5, 256, 32),
          "TileMap padding resolved to a cell");
  require(!editor::tileMapCellAt({321.0f, 321.0f}, 1, 256, 32),
          "a position resolved to a TileMap beyond the authored count");
}

}  // namespace

int main() {
  try {
    mapsUseFourColumnsAndOneCellOfPadding();
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
