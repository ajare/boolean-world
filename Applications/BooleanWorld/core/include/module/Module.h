#pragma once

#include <cstdint>

extern "C" {

int mod_create_world(float size);

int mod_destroy_world();

int mod_set_world_name(char const* name);

int mod_serialize_world(char const* filename);

int mod_create_regular_polygon(uint32_t operation, uint32_t fillType, uint32_t numSides, uint32_t materialIndex);

int mod_create_rectangle_polygon(uint32_t operation, uint32_t fillType, float xyRatio, uint32_t materialIndex);

int mod_create_torus_polygon(uint32_t operation, uint32_t fillType, float thickness, float resolution, uint32_t materialIndex);

int mod_set_primitive_layer(uint8_t layer);

int mod_set_primitive_priority(uint8_t priority);

int mod_set_primitive_floor_z(float z);

int mod_set_primitive_ceiling_z(float z);

int mod_set_primitive_flags(uint32_t flags);

int mod_add_primitive_flags(uint32_t flags);

int mod_remove_primitive_flags(uint32_t flags);

int mod_set_primitive_time_update_distance(float distance);

int mod_set_primitive_position(float x, float y);

int mod_set_primitive_transform_offset(float x, float y);

int mod_set_primitive_size(float width, float height);

int mod_set_primitive_influence_eye_origin_offset(float x, float y);

int mod_set_primitive_influence_eye_angle_offset(float angle);

int mod_set_primitive_follow_orbit_angle(bool follow);

int mod_set_primitive_animation_value(uint32_t key, float const* values, int numValues);

int mod_set_primitive_transform_0_input(uint32_t key, uint32_t index, uint32_t inputType);

int mod_set_primitive_transform_0_constant(uint32_t key, uint32_t index, float value);

int mod_set_primitive_transform_0_function(uint32_t key, uint32_t index, uint32_t fn, float value);

int mod_set_primitive_transform_0_operation(uint32_t key, uint32_t op);
}