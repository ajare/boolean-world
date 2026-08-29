#include "core/LiquidProperties.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace bw {
namespace core {

using namespace std;

namespace {

// Indexed by LiquidType, so entries stay in the enum's order.
constexpr array<LiquidProperties, LiquidTypeCount> liquidProperties{{
    // Water is the reference liquid, so its density is 1 by definition and
    // BW_PLAYER_DENSITY is written relative to it. The viscosity is tuned
    // against the player: it leaves an unmoving swimmer drifting back up to
    // the surface at a wallow rather than a bob, while still letting a fall
    // from a height drive them well under, and gives a swimming player about
    // 30 units per second of vertical speed while fully submerged.
    {1.0f, 12.0f},
}};

}  // namespace

LiquidProperties const& GetLiquidProperties(LiquidType type) {
  auto index = static_cast<int32_t>(type);
  if (index < 0 || index >= LiquidTypeCount) {
    return liquidProperties[0];
  }
  return liquidProperties[static_cast<size_t>(index)];
}

}  // namespace core
}  // namespace bw
