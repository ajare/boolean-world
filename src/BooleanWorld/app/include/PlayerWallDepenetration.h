#pragma once

#include <span>

#include <willpower/common/Vector2.h>

#include <common/GameDefines.h>

namespace bw::app {

// One collision line as createWorldCollisions hands it to the simulation.
struct WallSegment {
  wp::Vector2 v0;
  wp::Vector2 v1;
};

// The gap left between the collider and a wall it has been lifted off. Matches
// the clearance WorldCollisionSim's sliding response leaves behind, so a player
// already resting against a wall is never nudged again.
inline constexpr float WallDepenetrationMargin = 0.001f;

// Where a collider of `radius` must sit to be clear of every wall it is
// currently inside. Returns `position` unchanged when it already is.
//
// A wall can be reinstated underneath the player rather than approached: a
// tall floor step is withheld while they fall past it (see
// isDescendingForTallStepTraversal) and comes back the moment they land, by
// which time their collider may straddle it. A collider that starts a frame
// intersecting a line cannot move at all - willpower's sweep abandons any
// movement that ends still intersecting, which for an already-overlapping
// collider is every direction at once, in or out. Lifting it clear before the
// sweep restores movement without opening the wall: it still blocks, it just
// blocks from the side the player is actually on.
[[nodiscard]] inline wp::Vector2 resolveWallOverlap(
    wp::Vector2 position,
    float radius,
    std::span<WallSegment const> walls,
    uint32_t maxIterations = 4) {
  auto clearance = radius + WallDepenetrationMargin;
  for (uint32_t iteration = 0; iteration < maxIterations; ++iteration) {
    auto moved = false;
    for (auto const& wall : walls) {
      auto distance = position.distanceToLine(wall.v0, wall.v1);
      if (distance >= radius) {
        continue;
      }
      auto closestPoint = position.closestPointOnLine(wall.v0, wall.v1);
      auto outward = position - closestPoint;
      if (outward.normalise() <= 1e-4f) {
        // Dead centre on the line: there is no side to be lifted towards, and
        // guessing one could push the player through the wall.
        continue;
      }
      position = closestPoint + outward * clearance;
      moved = true;
    }
    if (!moved) {
      break;
    }
  }
  return position;
}

}  // namespace bw::app
