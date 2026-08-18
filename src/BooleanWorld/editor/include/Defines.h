#pragma once

#include <common/GameDefines.h>

#define ED_WINDOW_WIDTH 1440
#define ED_WINDOW_HEIGHT 900
#define ED_VERTEX_RENDER_SIZE 4.0
#define ED_MAX_NAME_LENGTH 16

#define ED_DEFAULT_WORLD_SIZE 8192.0f
#define ED_DEFAULT_WORLD_ACCEL_GRID_SIZE 512.0f

#define ED_GHOST_INDEX 0

// Creation/edit params
#define ED_MIN_REGULAR_POLYGON_SIDES 3
#define ED_MAX_REGULAR_POLYGON_SIDES 8

#define ED_MIN_ARC_LENGTH 0.01f
#define ED_MAX_ARC_LENGTH 360.0f
#define ED_MIN_CIRCLE_RESOLUTION (3.0f / 64.0f)

#define ED_MIN_RECTANGLE_XYRATIO 1.0f
#define ED_MAX_RECTANGLE_XYRATIO 10.0f

#define ED_MIN_SUPERFORMULA_RESOLUTION 0.01f
#define ED_MIN_SUPERFORMULA_A 1.0f  // Cannot be lower than 1, otherwise SuperformulaPrimitive::getRadius() breaks
#define ED_MAX_SUPERFORMULA_A 2.0f
#define ED_MIN_SUPERFORMULA_B 1.0f  // Cannot be lower than 1, otherwise SuperformulaPrimitive::getRadius() breaks
#define ED_MAX_SUPERFORMULA_B 2.0f
#define ED_MIN_SUPERFORMULA_M 0.1f
#define ED_MAX_SUPERFORMULA_M 10.0f
#define ED_MIN_SUPERFORMULA_N1 0.1f
#define ED_MAX_SUPERFORMULA_N1 10.0f
#define ED_MIN_SUPERFORMULA_N2 0.1f
#define ED_MAX_SUPERFORMULA_N2 10.0f
#define ED_MIN_SUPERFORMULA_N3 0.1f
#define ED_MAX_SUPERFORMULA_N3 10.0f

#define ED_MIN_PRIMITIVE_SIZE 10.0f
#define ED_MAX_PRIMITIVE_SIZE 512.0f

#define ED_MIN_TRANSFORM_CONSTANT -1.0f
#define ED_MAX_TRANSFORM_CONSTANT 1.0f

#define ED_PRIM_HANDLE_TOP_LEFT 0
#define ED_PRIM_HANDLE_TOP 1
#define ED_PRIM_HANDLE_TOP_RIGHT 2
#define ED_PRIM_HANDLE_RIGHT 3
#define ED_PRIM_HANDLE_BOTTOM_RIGHT 4
#define ED_PRIM_HANDLE_BOTTOM 5
#define ED_PRIM_HANDLE_BOTTOM_LEFT 6
#define ED_PRIM_HANDLE_LEFT 7
#define ED_PRIM_HANDLE_RADIUS 10
