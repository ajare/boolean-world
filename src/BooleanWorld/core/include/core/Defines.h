#pragma once

#include <cstdint>

// Type for storing a frame number.  This increments every frame and so needs to be large enough
// to store a long play time.  At 60FPS, 32 bits is (2^32)/(60*60*60*24) = 828 days
typedef int64_t frame_number_type;

// Maths etc
#define BW_PI 3.14159265359
#define BW_TWOPI (BW_PI * 2)

// World size.  This is the fixed dimension in both axes, and is centred on the origin.
#define BW_WORLD_SIZE 8192

// This must be a power of two and less than 256, as we need to use the upper bit of a
// uint8_t for storing a special flag.
#define BW_PRIMITIVE_GRID_DIM_MAX 128

// Resource limits retained from the original world format.
#define BW_VERTEX_COUNT_USEABLE_MAX ((1 << 20) - 2)
#define BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX (1 << 10)
#define BW_WORLD_PRIMITIVE_COUNT_MAX ((1 << 14) - 1)

#define BW_PRIORITY_MIN_VALUE 0
#define BW_PRIORITY_MAX_VALUE 255

#define BW_PRIMITIVE_INTERACTS_FLAG 0x0001                // Does this primitive participate in triangulation?
#define BW_PRIMITIVE_GHOST_FLAG 0x0002                    // For the editor: indicates this should not be loaded into a game.
#define BW_PRIMITIVE_NO_TIME_UPDATE_PLAYER_STATIC 0x0004  // Don't update Primitive time if player isn't moving
#define BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE 0x0008     // Don't update Primitive time if visible to player
#define BW_PRIMITIVE_EXACT_BOUNDS_FLAG 0x0010             // Calculate exact bounds using vertices

// Per-edge flags stored on a MeshPrimitive Vertex, describing the edge FROM
// that vertex TO THE NEXT vertex in its Ring (the "outgoing edge").
#define BW_MESH_EDGE_COLLIDES_FLAG 0x0001  // Value of an authored collision override.
// Set when BW_MESH_EDGE_COLLIDES_FLAG is authored rather than generation-time.
#define BW_MESH_EDGE_COLLISION_OVERRIDE_FLAG 0x0004
// Deliberately inverted polarity (set = hidden): files serialized before
// this bit existed have it clear, so they read as visible with no
// migration needed. Collision flags use an explicit format marker because
// their legacy default requires migration to the tri-state representation.
#define BW_MESH_EDGE_INVISIBLE_FLAG 0x0002  // This edge's wall does not render, when External.

// This is the maximum size that an interpolator for distance can take.  In particular this is used for influence zones.
#define BW_INTERPOLATOR_MAX_DISTANCE 500.0f
#define BW_INTERPOLATOR_MAX_SCALE 10.0f
#define BW_INTERPOLATOR_MAX_ANGLE 360.0f

// The extra padding that is added on to the Player's view distance when calculating how close a Primitive
// needs to be (taking its size into consideration), in order for the Primitive to update its Vertex positions.
#define BW_PRIMITIVE_VERTEX_CALC_DIST_PADDING 128

//
// Global events triggered by Primitive animation
//

// Execute a clip on this frame.  Useful for when a Primitive has connected others together.
#define BW_PRIMITIVE_GLOBAL_EVENT_CLIP 0x00000002

//
// Material definitions
//
#define BW_MATERIAL_COUNT 39
#define BW_MATERIAL_PARAMS_MAX 8

// Reserved MATERIAL_INDEX for a wall's back face (the side its normal points
// away from): a flat, plain-white matte procedural material in
// world_pbr.frag (still lit like any other material, just untextured),
// never offered as an authorable material and never matched by a real
// Primitive's wallMaterialIndex.
#define BW_WALL_BACK_FACE_MATERIAL_INDEX 39

// A materialIndex a Sub-material id resolves to when the id doesn't name any
// Sub-material in a loaded ProcMaterial catalog (missing/unknown/empty) -
// see SubMaterialResolver (BooleanWorld/app). Deliberately outside every
// real Technique's and BW_WALL_BACK_FACE_MATERIAL_INDEX's range, so it hits
// world_pbr.frag/world_pbr_2d.frag's switch(materialIndex) default case (an
// error material) rather than aliasing a real Technique.
#define BW_MATERIAL_ERROR_INDEX 999
