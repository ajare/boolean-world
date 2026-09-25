#pragma once

#include <core/WorldData.h>
#include <willpower/common/Vector2.h>

namespace bw::app {

struct PlayerTorchPlacement {
  wp::Vector2 position;
  float elevation{};
};

// Trace the configured reach from the player each frame. Distance is measured
// along the path, including any Portal crossings, rather than in one space.
[[nodiscard]] PlayerTorchPlacement placePlayerTorch(
    core::WorldData const& world, wp::Vector2 position, float elevation,
    wp::Vector2 direction, float maximumDistance);

}  // namespace bw::app
