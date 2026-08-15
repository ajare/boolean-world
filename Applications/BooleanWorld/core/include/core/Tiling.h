#pragma once

#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"

namespace bw {
namespace core {
struct TilingTile {
  wp::Vector2 centre;
  float angle;
  uint32_t subType;
};

class BW_API Tiling {
  float mBaseSize;

protected:
  float getBaseSize() const;

public:
  explicit Tiling(float baseSize);

  virtual ~Tiling() = default;

  virtual std::vector<TilingTile> generateTiles(float sizeX, float sizeY) const = 0;

  virtual TilingTile createPrototypeTile(wp::Vector2 const& centre, float angle, uint32_t subType) const = 0;

  virtual std::vector<wp::Vector2> generateTileOutline(wp::Vector2 const& centre, float angle, uint32_t subType) const = 0;
};

}  // namespace core
}  // namespace bw