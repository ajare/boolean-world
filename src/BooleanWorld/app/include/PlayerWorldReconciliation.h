#pragma once

#include <cmath>

#include <core/ArrangementWorldData.h>

#include <common/GameDefines.h>

#include "PlayerVerticalPhysics.h"

namespace bw::app {

// The vertical and validity consequences of replacing one immutable World
// snapshot with another while the player remains at the same World-plane
// position. Horizontal wall overlap is deliberately left to the existing
// collision depenetration pass.
struct PlayerWorldReconciliation {
  PlayerVerticalState vertical;
  bool regrounded{false};
  bool supportLost{false};
  bool requiresLocationRecovery{false};
};

[[nodiscard]] inline PlayerWorldReconciliation reconcilePlayerAfterWorldRebuild(
    bw::core::ArrangementWorldData const& previousWorld,
    bw::core::ArrangementWorldData const& rebuiltWorld,
    wp::Vector2 const& position,
    PlayerVerticalState vertical) {
  constexpr float ElevationEpsilon = 0.001f;
  auto previousSurface = previousWorld.getSurfaceSample(position);
  auto rebuiltSurface = rebuiltWorld.getSurfaceSample(position);
  auto wasGrounded =
      previousSurface &&
      std::abs(vertical.feetElevation - previousSurface->floorElevation) <=
          ElevationEpsilon &&
      std::abs(vertical.verticalVelocity) <= ElevationEpsilon;

  PlayerWorldReconciliation result{vertical};
  if (!rebuiltSurface) {
    result.supportLost = wasGrounded;
    result.requiresLocationRecovery = true;
    return result;
  }

  auto standingClearance = rebuiltSurface->ceilingElevation -
                           rebuiltSurface->floorElevation;
  if (standingClearance < BW_PLAYER_HEIGHT) {
    result.requiresLocationRecovery = true;
    return result;
  }

  // A newly raised floor must not be allowed to enter a grounded player's
  // body. Snap onto it as a regenerated support surface, rather than treating
  // the authored World change as an ordinary step that takes time to climb.
  if (wasGrounded &&
      rebuiltSurface->floorElevation >=
          vertical.feetElevation - ElevationEpsilon) {
    result.vertical.feetElevation = rebuiltSurface->floorElevation;
    result.vertical.verticalVelocity = 0.0f;
    result.regrounded = true;
  } else if (
      wasGrounded && rebuiltSurface->floorElevation <
                         vertical.feetElevation - ElevationEpsilon) {
    // Leave the feet where they were. The ordinary vertical-physics step now
    // sees empty space below them and begins a fall in the rebuilt World.
    result.supportLost = true;
  }

  // Rebuilding a ceiling through an airborne player, or a floor through a
  // player who was not grounded, cannot be repaired by vertical grounding.
  // Route it through the play state's existing invalid-location path instead.
  result.requiresLocationRecovery =
      result.vertical.feetElevation <
          rebuiltSurface->floorElevation - ElevationEpsilon ||
      result.vertical.feetElevation + BW_PLAYER_HEIGHT >
          rebuiltSurface->ceilingElevation + ElevationEpsilon;
  return result;
}

}  // namespace bw::app
