#pragma once

#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"
#include "core/Tiling.h"

namespace bw {
namespace core {
class BW_API SquareTiling : public Tiling {
public:
  explicit SquareTiling(float baseSize);

  std::vector<TilingTile> generateTiles(float sizeX, float sizeY) const override;

  TilingTile createPrototypeTile(wp::Vector2 const& centre, float angle, uint32_t subType) const override;

  std::vector<wp::Vector2> generateTileOutline(wp::Vector2 const& centre, float angle, uint32_t subType) const override;
};

}  // namespace core
}  // namespace bw