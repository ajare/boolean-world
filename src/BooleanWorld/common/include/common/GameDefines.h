#pragma once

#define BW_PLAYER_RADIUS 6.0f
#define BW_PLAYER_HEIGHT 20.0f
#define BW_PLAYER_EYE_HEIGHT (BW_PLAYER_HEIGHT * 0.9f)
#define BW_PLAYER_STEP_HEIGHT 8.0f

// Vertical physics: how fast the player rises onto a taller floor (already
// admitted by collision's step-height/clearance rules) rather than
// snapping instantly, and how fast they accelerate downward once walked
// past the edge of the floor beneath them.
#define BW_PLAYER_STEP_SPEED 120.0f
#define BW_PLAYER_GRAVITY 500.0f

// How far short of a wall the Player Torch stops when the configured debug
// distance would carry it through one. Placing it exactly on the surface
// would leave it coplanar with the quad that occludes it, so it has to end up
// on the near side by a definite margin.
#define BW_PLAYER_TORCH_WALL_CLEARANCE 1.0f

#define BW_PLAYER_VIEW_DISTANCE 192.0f
#define BW_PLAYER_FOV 60.0f

#define BW_PLAYER_SPEED 60.0f

// The player's own mass per unit volume, on the same scale as a liquid's
// LiquidProperties::density (core/LiquidProperties.h), where water is 1. The
// two divide out: the player floats with this over the liquid's density of
// their height submerged, so in water they rest 85% under with their head
// just clear, and a denser liquid floats them higher. Being lighter than
// water is what makes them float at all - at a density above the liquid's
// they would sink instead.
//
// This governs the whole of buoyancy, including how much of their weight the
// liquid carries off their feet: traction against the floor runs out at the
// same submersion this floats them at, which is when they stop wading and
// start swimming.
#define BW_PLAYER_DENSITY 0.85f

// Floor on how slow buoyancy can make the player: even weightless and fully
// submerged, swimming still makes some headway.
#define BW_PLAYER_MIN_SWIM_SPEED_FACTOR 0.35f

// How hard the player swims vertically, as an acceleration rather than a
// speed: it is worked against the liquid's viscosity, so the speed it
// achieves is this over that viscosity (about 30 units per second while fully
// submerged in water), and thicker liquid is harder to swim through without
// needing its own player constant. Applying it as a force also means a kick
// takes a few frames to build and bleeds away when released, rather than
// snapping to full speed and stopping dead. It has to beat the buoyant force
// pushing the player back up or they could never dive.
#define BW_PLAYER_SWIM_ACCELERATION 360.0f

// Water deep enough to submerge at least this fraction of the player's
// height (standing on the real floor) is deep enough to swim in rather than
// wade through: fly controls take over, and this is also the floor on how
// far the player can rise toward the surface under their own power - they
// can dive as deep as the floor allows, but not climb out on top of the
// water via vertical swim input alone.
#define BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION 0.7f

// How far an adjacent floor may be from the swimmer's eye level and still be
// mantled onto from the water. Beyond this the ledge is out of reach and the
// swimmer stays in the liquid. Deliberately larger than BW_PLAYER_STEP_HEIGHT:
// hauling yourself out of water uses your arms, and buoyancy has already
// lifted most of your weight.
#define BW_PLAYER_MANTLE_WATER 12.0f
// Clear of the crossed edge by this much on landing, so the climb never ends
// with the collider resting exactly on a wall it must then be pushed off.
#define BW_PLAYER_CLIMB_OUT_MARGIN 0.25f

#define BW_WORLD_FLOOR_HEIGHT_MIN -200.0f
#define BW_WORLD_CEILING_HEIGHT_MAX 200.0f
