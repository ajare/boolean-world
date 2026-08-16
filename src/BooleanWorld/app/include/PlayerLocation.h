#pragma once

#include <cstdint>

#include <willpower/common/Vector2.h>

namespace bw::core {
class ArrangementWorldData;
}

namespace bw::app {

struct PlayerLocation {
  int32_t faceIndex{-1};
  int32_t intersectingWallIndex{-1};
};

[[nodiscard]] PlayerLocation evaluatePlayerLocation(
    core::ArrangementWorldData const& worldData,
    wp::Vector2 const& resolvedPosition,
    float radius);

}  // namespace bw::app
