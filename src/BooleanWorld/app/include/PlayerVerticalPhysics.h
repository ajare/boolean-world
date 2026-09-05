#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <core/LiquidProperties.h>

#include <common/GameDefines.h>

namespace bw::app {

// Where the player is vertically, and how fast that is changing. floorZ is the
// height of the player's feet, not the floor beneath them - the floor is only
// what it rests on when they are grounded.
struct PlayerVerticalState {
  float floorZ{0.0f};
  float verticalVelocity{0.0f};
};

// Everything one vertical step needs to know about the world around the
// player. Kept free of the play state so the rule can be exercised on its own.
struct PlayerVerticalInputs {
  // False when the player is off the arrangement entirely, in which case
  // targetFloor carries no meaning.
  bool inWorld{true};
  float targetFloor{0.0f};
  // -infinity where the face holds no liquid.
  float liquidSurface{-std::numeric_limits<float>::infinity()};
  bw::core::LiquidProperties liquid{};
  // How hard fly controls are asking the swimmer up or down this frame.
  float swimEffort{0.0f};
  float frameTime{0.0f};
};

// A body floats with its own density over the liquid's of itself submerged.
// A liquid with no density carries nothing, so the player is never held up
// by it at any depth.
[[nodiscard]] inline float buoyantEquilibriumFraction(
    bw::core::LiquidProperties const& liquid) {
  if (liquid.density <= 0.0f) {
    return std::numeric_limits<float>::infinity();
  }
  return BW_PLAYER_DENSITY / liquid.density;
}

[[nodiscard]] inline PlayerVerticalState stepPlayerVerticalPhysics(
    PlayerVerticalState state, PlayerVerticalInputs const& inputs) {
  if (!inputs.inWorld) {
    // Off the edge of the arrangement entirely (eg. walked through a
    // non-colliding wall) - there is no face to read a floor height from,
    // and the floor query would return -infinity here. Freeze in place
    // rather than free-falling forever, so re-entering the world resumes
    // from the height last held while still on a face.
    state.verticalVelocity = 0.0f;
    return state;
  }

  auto targetFloor = inputs.targetFloor;
  auto frameTime = inputs.frameTime;
  auto liquidSurface = inputs.liquidSurface;
  // The height a floating player settles at. Liquid shallower than the
  // swimming threshold puts this below its own floor, in which case the floor
  // wins and the player wades rather than floats.
  auto floatZ = liquidSurface - BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION *
                                    float(BW_PLAYER_HEIGHT);
  // Submersion is measured at the player's own height, so walking off the edge
  // of a deep pool is an ordinary fall until they actually reach the water.
  // Liquid carries part of the player's weight either where it is deep enough
  // to lift them off the bottom, or while they are still dropping through it
  // after a fall - in both cases gravity alone no longer describes the motion.
  // Standing on the bottom of a shallow pool is neither, and falls through to
  // the ordinary ground logic so wading and steps keep working.
  auto const& liquid = inputs.liquid;
  auto equilibriumFraction = buoyantEquilibriumFraction(liquid);
  auto equilibriumZ =
      liquidSurface - equilibriumFraction * float(BW_PLAYER_HEIGHT);
  auto inLiquid = std::isfinite(liquidSurface) &&
                  liquidSurface > state.floorZ &&
                  (equilibriumZ > targetFloor || state.floorZ > targetFloor);
  if (inLiquid) {
    // Archimedes: the upward force goes with the submerged volume, which for a
    // uniform cylinder is just the submerged fraction of its height. Balanced
    // against weight at the fraction the player floats at, so a resting player
    // feels no net force, a fully submerged one rises, and one barely dipped
    // still falls at close to full gravity.
    auto submergedFraction = std::clamp(
        (liquidSurface - state.floorZ) / float(BW_PLAYER_HEIGHT), 0.0f, 1.0f);
    auto buoyantAcceleration =
        BW_PLAYER_GRAVITY * (submergedFraction / equilibriumFraction - 1.0f);

    // Swim input (fly controls, see EntityHandlerBooleanWorld::peekInput) is a
    // force worked against the liquid like any other, not a rate the player is
    // moved at, so the drag below governs it too - a kick builds speed over a
    // few frames and bleeds away again when released, rather than snapping
    // straight to full speed and stopping dead, and the speed it reaches falls
    // out of the liquid's viscosity rather than being stated separately.
    auto swimAcceleration = inputs.swimEffort * BW_PLAYER_SWIM_ACCELERATION;

    // Entry speed is carried in rather than discarded: whatever gravity built
    // up on the way down is still here on the first submerged frame, so a
    // plunge from a height drives the player deep before buoyancy returns them
    // to the surface, while stepping in from the bank barely dips them.
    state.verticalVelocity +=
        (buoyantAcceleration + swimAcceleration) * frameTime;

    // Liquid resists motion through it - what arrests a plunge, holds the rise
    // back to a wallow, and stops the player oscillating about their float
    // height once buoyancy has caught them. Only the submerged part of the
    // player meets that resistance, so the drag eases as they near the surface
    // and the last of a rise accelerates as they break out of the liquid;
    // going the other way, someone dropping in meets little resistance until
    // they are properly under.
    state.verticalVelocity -=
        state.verticalVelocity *
        std::min(1.0f, liquid.viscosity * submergedFraction * frameTime);

    auto previousFloorZ = state.floorZ;
    state.floorZ += state.verticalVelocity * frameTime;

    if (state.floorZ <= targetFloor) {
      // Reached the bottom - a hard stop, but any upward swim input still
      // lifts off again.
      state.floorZ = targetFloor;
      state.verticalVelocity = std::max(state.verticalVelocity, 0.0f);
    }

    // A barrier the player cannot rise through, rather than a ceiling that
    // pulls them down to it: buoyancy and swim input together carry them no
    // higher than their float height (see
    // BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION), but someone dropping in from
    // above starts above that height and must be left to sink past it under
    // their own momentum.
    auto maxFloorZ = std::max(targetFloor, floatZ);
    if (previousFloorZ <= maxFloorZ && state.floorZ > maxFloorZ) {
      state.floorZ = maxFloorZ;
      state.verticalVelocity = std::min(state.verticalVelocity, 0.0f);
    }
    return state;
  }

  if (targetFloor >= state.floorZ) {
    // Horizontal collision already refused any step too tall to climb (see
    // ArrangementWorldData's step-height/clearance rules), so any floor
    // rise reaching here is a walkable step: climb it smoothly rather than
    // snapping straight to it, and stay grounded (no carried fall speed).
    // tryClimbOutOfLiquid deliberately routes through here too, leaving the
    // player below their new bank so this smooths the haul out of the liquid.
    state.verticalVelocity = 0.0f;
    state.floorZ +=
        std::min(targetFloor - state.floorZ, BW_PLAYER_STEP_SPEED * frameTime);
  } else {
    // Walked past the edge of the floor beneath us: accelerate downward
    // under gravity until the new, lower floor catches us.
    state.verticalVelocity -= BW_PLAYER_GRAVITY * frameTime;
    state.floorZ += state.verticalVelocity * frameTime;
    if (state.floorZ <= targetFloor) {
      state.floorZ = targetFloor;
      state.verticalVelocity = 0.0f;
    }
  }
  return state;
}

}  // namespace bw::app
