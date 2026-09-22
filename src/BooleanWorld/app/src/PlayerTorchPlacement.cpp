#include "PlayerTorchPlacement.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <common/GameDefines.h>

namespace bw::app {
namespace {
constexpr float exitEpsilon = 0.001f;
// Independent of the rendering recursion budget. Very long/cyclic debug
// reaches must still terminate; stop before the next aperture at this limit.
constexpr uint32_t maximumCrossings = 64;
}

PlayerTorchPlacement placePlayerTorch(
    core::WorldData const& world, wp::Vector2 position, float elevation,
    wp::Vector2 direction, float maximumDistance) {
  auto remaining = std::max(0.0f, maximumDistance);
  if (direction.lengthSq() == 0.0f || remaining == 0.0f) {
    return {position, elevation};
  }
  direction.normalise();
  for (uint32_t crossings = 0; ; ++crossings) {
    core::ResolvedPortalPair const* nearestPair = nullptr;
    uint32_t nearestEndpoint = 0;
    float nearestDistance = std::numeric_limits<float>::infinity();
    for (auto const& pair : world.getPortalPairs()) {
      if (!pair.active) continue;
      for (uint32_t endpoint = 0; endpoint < 2; ++endpoint) {
        auto const& aperture = pair.endpoints[endpoint].aperture;
        auto side = (position - aperture.centre).dot(aperture.front);
        auto approach = direction.dot(aperture.front);
        if (side < 0.0f || approach >= -0.000001f ||
            elevation <= aperture.bottom || elevation >= aperture.top) continue;
        auto distance = -side / approach;
        if (distance > remaining || distance >= nearestDistance) continue;
        auto intersection = position + direction * distance;
        if (std::abs((intersection - aperture.centre).dot(aperture.tangent)) >=
            aperture.width * 0.5f) continue;
        nearestPair = &pair;
        nearestEndpoint = endpoint;
        nearestDistance = distance;
      }
    }

    auto wall = world.distanceToFirstWallCrossing(
        position, position + direction * remaining, elevation);
    // An active aperture replaces its supporting wall at the same distance.
    // A nearer ordinary wall must still stop the Torch before that aperture.
    if (!nearestPair || (wall && *wall < nearestDistance - exitEpsilon)) {
      auto distance = wall
          ? std::clamp(*wall - BW_PLAYER_TORCH_WALL_CLEARANCE, 0.0f, remaining)
          : remaining;
      return {position + direction * distance, elevation};
    }
    if (crossings == maximumCrossings) {
      return {position + direction * std::max(
          0.0f, nearestDistance - BW_PLAYER_TORCH_WALL_CLEARANCE), elevation};
    }

    auto transform = core::BuildPortalRigidTransform(*nearestPair, nearestEndpoint);
    position = transform.transformPoint(position + direction * nearestDistance);
    direction = transform.transformVector(direction);
    elevation = transform.transformElevation(elevation);
    remaining = std::max(0.0f, remaining - nearestDistance);
    // Keep the next wall query off the supporting exit plane. This small
    // normal bias, like Player traversal's exit bias, prevents self-contact.
    position += transform.destination.front * exitEpsilon;
    if (remaining == 0.0f) return {position, elevation};
  }
}
}  // namespace bw::app
