#include "core/SquareTiling.h"

namespace bw {
namespace core {

using namespace std;

SquareTiling::SquareTiling(float baseSize)
    : Tiling(baseSize) {
}

vector<TilingTile> SquareTiling::generateTiles(float sizeX, float sizeY) const {
  vector<TilingTile> tiles;
  auto baseSize = getBaseSize();
  auto b2 = baseSize / 2;

  int tilesX = (int)(sizeX / baseSize);
  int tilesY = (int)(sizeY / baseSize);

  tiles.reserve(tilesX * tilesY);

  float y0 = -(tilesY * baseSize) / 2;

  for (int y = 0; y < tilesY; ++y) {
    float x0 = -(tilesX * baseSize) / 2;

    for (int x = 0; x < tilesX; ++x) {
      tiles.push_back({{x0 + b2, y0 + b2}, 0.0f, 0});
      x0 += baseSize;
    }

    y0 += baseSize;
  }

  return tiles;
}

TilingTile SquareTiling::createPrototypeTile(wp::Vector2 const& centre, float angle, uint32_t subType) const {
  return {centre, angle, subType};
}

vector<wp::Vector2> SquareTiling::generateTileOutline(wp::Vector2 const& centre, float angle, uint32_t subType) const {
  BW_UNUSED(angle);
  BW_UNUSED(subType);

  auto b2 = getBaseSize() / 2;

  return {
      {centre.x - b2, centre.y + b2},
      {centre.x + b2, centre.y + b2},
      {centre.x + b2, centre.y - b2},
      {centre.y - b2, centre.y - b2}};
}

}  // namespace core
}  // namespace bw