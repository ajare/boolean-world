#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include <core/ArrangementWorldData.h>

#include <common/GameDefines.h>

namespace bw::app {

// The vertical consequences of one already-resolved horizontal movement.
// Horizontal wall collision remains responsible for Border and Step walls;
// this result handles constraints and support that exist within the affine
// face segments between those walls.
struct PlayerSurfaceTraversalResult {
  // One unless a floor/ceiling pair becomes too narrow within a traversed
  // affine segment. The limit is where standing clearance reaches exactly the
  // player's height.
  float allowedFraction{1.0f};
  bool clearanceBlocked{false};

  // Present only when the player began grounded and every crossed floor joined
  // continuously. The caller may put the feet directly on this floor: this is
  // following one continuous surface, not climbing a Step.
  std::optional<float> supportedFloorElevation;
};

[[nodiscard]] inline PlayerSurfaceTraversalResult evaluatePlayerSurfaceTraversal(
    bw::core::ArrangementWorldData const& world,
    wp::Vector2 const& start,
    wp::Vector2 const& end,
    float feetElevation,
    float verticalVelocity) {
  PlayerSurfaceTraversalResult result;
  auto segments = world.getSurfaceTraversal(start, end);
  if (segments.empty()) return result;

  constexpr float ElevationEpsilon = 0.001f;
  auto const& first = segments.front();
  auto supported = first.beginSurface &&
                   std::abs(feetElevation -
                            first.beginSurface->floorElevation) <=
                       ElevationEpsilon &&
                   std::abs(verticalVelocity) <= ElevationEpsilon;

  for (size_t index = 0; index < segments.size(); ++index) {
    auto const& segment = segments[index];
    if (!segment.beginSurface || !segment.endSurface) {
      supported = false;
      continue;
    }

    // Both boundaries are affine over this segment, hence their difference is
    // affine too. Its minimum is at an endpoint; if it crosses player height,
    // interpolation gives the exact first impassable position without frame-
    // rate-dependent sampling.
    auto beginClearance = segment.beginSurface->ceilingElevation -
                          segment.beginSurface->floorElevation;
    auto endClearance = segment.endSurface->ceilingElevation -
                        segment.endSurface->floorElevation;
    if (beginClearance < BW_PLAYER_HEIGHT) {
      result.allowedFraction = segment.beginFraction;
      result.clearanceBlocked = true;
      supported = false;
      break;
    }
    if (endClearance < BW_PLAYER_HEIGHT) {
      auto alongSegment =
          (beginClearance - BW_PLAYER_HEIGHT) /
          (beginClearance - endClearance);
      result.allowedFraction = std::clamp(
          segment.beginFraction +
              (segment.endFraction - segment.beginFraction) * alongSegment,
          segment.beginFraction, segment.endFraction);
      result.clearanceBlocked = true;
      break;
    }

    if (index + 1 < segments.size()) {
      auto const& next = segments[index + 1];
      if (!next.beginSurface) {
        supported = false;
        continue;
      }
      auto floorDiscontinuity = next.beginSurface->floorElevation -
                                segment.endSurface->floorElevation;
      if (std::abs(floorDiscontinuity) > ElevationEpsilon) {
        // A rise is an ordinary Step and is climbed by vertical physics. A
        // drop removes support and begins an ordinary fall. Neither may turn
        // into slope-following merely because a later segment descends.
        supported = false;
      }
    }
  }

  if (supported) {
    auto position = start + (end - start) * result.allowedFraction;
    auto segment = std::find_if(
        segments.begin(), segments.end(), [&](auto const& candidate) {
          return result.allowedFraction <= candidate.endFraction + 1.0e-5f;
        });
    if (segment != segments.end()) {
      auto surface = world.getSurfaceSample(segment->faceIndex, position);
      if (surface) result.supportedFloorElevation = surface->floorElevation;
    }
  }
  return result;
}

}  // namespace bw::app
