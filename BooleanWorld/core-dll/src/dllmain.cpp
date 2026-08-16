#include <Windows.h>
#include <cstdint>
#include <stdio.h>

#include <module/Module.h>

extern "C" {
__declspec(dllexport) int create_world(float size) {
  return mod_create_world(size);
}

__declspec(dllexport) int destroy_world() {
  return mod_destroy_world();
}

__declspec(dllexport) int set_world_name(char const* name) {
  return mod_set_world_name(name);
}

__declspec(dllexport) int serialize_world(char const* filename) {
  return mod_serialize_world(filename);
}

__declspec(dllexport) int create_regular_polygon(uint32_t operation, uint32_t fillType, uint32_t numSides, uint32_t materialIndex) {
  return mod_create_regular_polygon(operation, fillType, numSides, materialIndex);
}

__declspec(dllexport) int create_torus_polygon(uint32_t operation, uint32_t fillType, float thickness, float resolution, uint32_t materialIndex) {
  return mod_create_torus_polygon(operation, fillType, thickness, resolution, materialIndex);
}

__declspec(dllexport) int create_rectangle_polygon(uint32_t operation, uint32_t fillType, float xyRatio, uint32_t materialIndex) {
  return mod_create_rectangle_polygon(operation, fillType, xyRatio, materialIndex);
}

__declspec(dllexport) int set_primitive_size(float width, float height) {
  return mod_set_primitive_size(width, height);
}

__declspec(dllexport) int set_primitive_layer(uint8_t layer) {
  return mod_set_primitive_layer(layer);
}

__declspec(dllexport) int set_primitive_priority(uint8_t priority) {
  return mod_set_primitive_priority(priority);
}

__declspec(dllexport) int set_primitive_floor_z(float z) {
  return mod_set_primitive_floor_z(z);
}

__declspec(dllexport) int set_primitive_ceiling_z(float z) {
  return mod_set_primitive_ceiling_z(z);
}

__declspec(dllexport) int set_primitive_flags(uint32_t flags) {
  return mod_set_primitive_flags(flags);
}

__declspec(dllexport) int add_primitive_flags(uint32_t flags) {
  return mod_add_primitive_flags(flags);
}

__declspec(dllexport) int remove_primitive_flags(uint32_t flags) {
  return mod_remove_primitive_flags(flags);
}

__declspec(dllexport) int set_primitive_time_update_distance(float distance) {
  return mod_set_primitive_time_update_distance(distance);
}

__declspec(dllexport) int set_primitive_position(float x, float y) {
  return mod_set_primitive_position(x, y);
}

__declspec(dllexport) int set_primitive_transform_offset(float x, float y) {
  return mod_set_primitive_transform_offset(x, y);
}

__declspec(dllexport) int set_primitive_influence_eye_origin_offset(float x, float y) {
  return mod_set_primitive_influence_eye_origin_offset(x, y);
}

__declspec(dllexport) int set_primitive_influence_eye_angle_offset(float angle) {
  return mod_set_primitive_influence_eye_angle_offset(angle);
}

__declspec(dllexport) int set_primitive_follow_orbit_angle(bool follow) {
  return mod_set_primitive_follow_orbit_angle(follow);
}

__declspec(dllexport) int set_primitive_animation_value(uint32_t key, float const* values, int numValues) {
  return mod_set_primitive_animation_value(key, values, numValues);
}

__declspec(dllexport) int set_primitive_transform_0_input(uint32_t key, uint32_t index, uint32_t inputType) {
  return mod_set_primitive_transform_0_input(key, index, inputType);
}

__declspec(dllexport) int set_primitive_transform_0_constant(uint32_t key, uint32_t index, float value) {
  return mod_set_primitive_transform_0_constant(key, index, value);
}

__declspec(dllexport) int set_primitive_transform_0_function(uint32_t key, uint32_t index, uint32_t fn, float value) {
  return mod_set_primitive_transform_0_function(key, index, fn, value);
}

__declspec(dllexport) int set_primitive_transform_0_operation(uint32_t key, uint32_t op) {
  return mod_set_primitive_transform_0_operation(key, op);
}
};

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }
  return TRUE;
}
