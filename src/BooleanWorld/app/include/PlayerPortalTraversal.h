#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include <willpower/common/Vector2.h>

namespace bw::core {
class ArrangementWorldData;
struct ResolvedPortalPair;
}  // namespace bw::core

namespace bw::app {

// Portal loops are valid across frames, but one movement update is finite.
// This budget is intentionally named and independent of the collision
// simulation's ordinary sliding-iteration limit.
inline constexpr uint32_t MaxPortalCrossingsPerUpdate = 6;
inline constexpr float PortalExitPlaneEpsilon = 0.01f;

struct PortalEndpointIdentity {
  uint32_t layerId{~0u};
  uint32_t pairId{~0u};
  uint8_t endpoint{0xff};

  auto operator<=>(PortalEndpointIdentity const&) const = default;
};

// Suppresses the endpoint just emerged from until the whole player collider
// has cleared its front side. This is geometric state, not a cooldown timer.
struct PortalExitSideState {
  PortalEndpointIdentity endpoint{};
  bool active{false};
};

struct PlayerPortalMotion {
  wp::Vector2 position{};
  float feetElevation{};
  float yaw{};
  float pitch{};
  wp::Vector2 horizontalVelocity{};
  float verticalVelocity{};
  wp::Vector2 unconsumedMovement{};
};

struct PortalRepeatedState {
  PortalEndpointIdentity endpoint{};
  wp::Vector2 crossingPosition{};
  wp::Vector2 unconsumedMovement{};
};

struct PlayerPortalUpdateState {
  uint32_t crossings{};
  std::vector<PortalRepeatedState> visited;
  PortalExitSideState exitSide;
  bool cameraCut{false};
  bool terminatedByBudgetOrRepeat{false};
};

enum class PlayerPortalCrossingResult : uint8_t {
  NotCrossing,
  // A valid front-side approach whose centre has not reached the plane yet.
  // The collision sweep must ignore the aperture, not stop at collider contact.
  Approaching,
  Blocked,
  Traversed
};

// Attempts one source endpoint against the complete swept centre movement.
// On success motion contains the destination position and transformed
// remaining displacement; pitch and vertical velocity are left unchanged.
[[nodiscard]] PlayerPortalCrossingResult tryPlayerPortalCrossing(
    core::ArrangementWorldData const& world,
    core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpoint,
    float playerRadius,
    float playerHeight,
    PlayerPortalMotion& motion,
    PlayerPortalUpdateState& updateState);

// Clears exit-side suppression only after the complete horizontal collider is
// in front of the destination plane. Calling this once at update start makes
// deliberate reverse traversal available without any elapsed-time rule.
void updatePortalExitSideState(
    core::ArrangementWorldData const& world,
    wp::Vector2 const& playerPosition,
    float playerRadius,
    PortalExitSideState& state);

}  // namespace bw::app
