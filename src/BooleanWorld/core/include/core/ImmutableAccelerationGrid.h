#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"

namespace bw::core {

// A compact, read-only spatial grid. Items are supplied once, in index order,
// and stored in flat cell ranges without reverse item maps or movement state.
class BW_API ImmutableAccelerationGrid {
public:
  using IndexCollection = std::vector<uint32_t>;

  struct ItemBounds {
    wp::Vector2 minExtent;
    wp::Vector2 maxExtent;
  };

private:
  wp::Vector2 mOffset;
  wp::Vector2 mSize;
  int mCellDimX;
  int mCellDimY;
  std::vector<uint32_t> mCellOffsets;
  IndexCollection mItems;

  void getItemsInArea(
      wp::Vector2 const& minExtent,
      wp::Vector2 const& maxExtent,
      IndexCollection& indices) const;

public:
  ImmutableAccelerationGrid(
      wp::Vector2 const& offset,
      wp::Vector2 const& size,
      int cellDimX,
      int cellDimY,
      std::span<ItemBounds const> itemBounds);

  [[nodiscard]] wp::Vector2 getCellSize() const;

  void getContainingCell(
      bool checkBounds,
      float x,
      float y,
      int& cellX,
      int& cellY) const;

  [[nodiscard]] std::span<uint32_t const> getCellItems(int x, int y) const;

  template <typename Area>
  void getCandidateItemsInBoundingArea(
      Area const& area,
      IndexCollection& indices) const {
    wp::Vector2 minExtent;
    wp::Vector2 maxExtent;
    area.getExtents(minExtent, maxExtent);
    getItemsInArea(minExtent, maxExtent, indices);
  }

  [[nodiscard]] size_t storageBytes() const;
};

}  // namespace bw::core
