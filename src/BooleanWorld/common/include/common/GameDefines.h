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

#define BW_PLAYER_VIEW_DISTANCE 192.0f
#define BW_PLAYER_FOV 60.0f

#define BW_PLAYER_SPEED 60.0f

#define BW_WORLD_FLOOR_HEIGHT_MIN -200.0f
#define BW_WORLD_CEILING_HEIGHT_MAX 200.0f
