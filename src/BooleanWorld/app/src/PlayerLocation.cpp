#include "PlayerLocation.h"

#include <core/ArrangementWorldData.h>

namespace bw::app {

PlayerLocation evaluatePlayerLocation(
    core::ArrangementWorldData const& worldData,
    wp::Vector2 const& resolvedPosition,
    float radius) {
  auto faceIndex = worldData.getContainingFaceIndex(resolvedPosition);
  return {
      faceIndex == ~0u ? -1 : static_cast<int32_t>(faceIndex),
      worldData.circleIntersectsWall(resolvedPosition, radius)};
}

}  // namespace bw::app
