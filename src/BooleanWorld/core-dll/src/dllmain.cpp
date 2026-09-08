#ifdef _WIN32
#include <Windows.h>
#define COREDLL_API __declspec(dllexport)
#else
#define COREDLL_API __attribute__((visibility("default")))
#endif
#include <cstdint>
#include <stdio.h>

#include <module/Module.h>

extern "C" {
COREDLL_API int create_world(float size) {
  return mod_create_world(size);
}

COREDLL_API int destroy_world() {
  return mod_destroy_world();
}

COREDLL_API int set_world_name(char const* name) {
  return mod_set_world_name(name);
}

COREDLL_API int serialize_world(char const* filename) {
  return mod_serialize_world(filename);
}

COREDLL_API int create_regular_polygon(uint32_t operation, uint32_t fillType, uint32_t numSides, uint32_t materialIndex) {
  return mod_create_regular_polygon(operation, fillType, numSides, materialIndex);
}

COREDLL_API int create_torus_polygon(uint32_t operation, uint32_t fillType, float thickness, float resolution, uint32_t materialIndex) {
  return mod_create_torus_polygon(operation, fillType, thickness, resolution, materialIndex);
}

COREDLL_API int create_rectangle_polygon(uint32_t operation, uint32_t fillType, float xyRatio, uint32_t materialIndex) {
  return mod_create_rectangle_polygon(operation, fillType, xyRatio, materialIndex);
}

COREDLL_API int set_primitive_size(float width, float height) {
  return mod_set_primitive_size(width, height);
}

COREDLL_API int set_primitive_priority(uint8_t priority) {
  return mod_set_primitive_priority(priority);
}

COREDLL_API int set_primitive_floor_z(float z) {
  return mod_set_primitive_floor_z(z);
}

COREDLL_API int set_primitive_floor_elevation(
    float base, float gradientX, float gradientY) {
  return mod_set_primitive_floor_elevation(base, gradientX, gradientY);
}

COREDLL_API int set_primitive_ceiling_z(float z) {
  return mod_set_primitive_ceiling_z(z);
}

COREDLL_API int set_primitive_ceiling_elevation(
    float base, float gradientX, float gradientY) {
  return mod_set_primitive_ceiling_elevation(base, gradientX, gradientY);
}

COREDLL_API int set_primitive_flags(uint32_t flags) {
  return mod_set_primitive_flags(flags);
}

COREDLL_API int add_primitive_flags(uint32_t flags) {
  return mod_add_primitive_flags(flags);
}

COREDLL_API int remove_primitive_flags(uint32_t flags) {
  return mod_remove_primitive_flags(flags);
}

COREDLL_API int set_primitive_time_update_distance(float distance) {
  return mod_set_primitive_time_update_distance(distance);
}

COREDLL_API int set_primitive_position(float x, float y) {
  return mod_set_primitive_position(x, y);
}

COREDLL_API int set_primitive_transform_offset(float x, float y) {
  return mod_set_primitive_transform_offset(x, y);
}

COREDLL_API int set_primitive_influence_eye_origin_offset(float x, float y) {
  return mod_set_primitive_influence_eye_origin_offset(x, y);
}

COREDLL_API int set_primitive_influence_eye_angle_offset(float angle) {
  return mod_set_primitive_influence_eye_angle_offset(angle);
}

COREDLL_API int set_primitive_follow_orbit_angle(bool follow) {
  return mod_set_primitive_follow_orbit_angle(follow);
}

COREDLL_API int set_primitive_animation_value(uint32_t key, float const* values, int numValues) {
  return mod_set_primitive_animation_value(key, values, numValues);
}

COREDLL_API int set_primitive_transform_0_input(uint32_t key, uint32_t index, uint32_t inputType) {
  return mod_set_primitive_transform_0_input(key, index, inputType);
}

COREDLL_API int set_primitive_transform_0_constant(uint32_t key, uint32_t index, float value) {
  return mod_set_primitive_transform_0_constant(key, index, value);
}

COREDLL_API int set_primitive_transform_0_function(uint32_t key, uint32_t index, uint32_t fn, float value) {
  return mod_set_primitive_transform_0_function(key, index, fn, value);
}

COREDLL_API int set_primitive_transform_0_operation(uint32_t key, uint32_t op) {
  return mod_set_primitive_transform_0_operation(key, op);
}
};

#ifdef _WIN32
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
#endif
