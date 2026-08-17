#include <Windows.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;

void Expect(char const* description, bool condition) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

template <typename Function>
Function LoadFunction(HMODULE module, char const* name) {
  auto function = reinterpret_cast<Function>(GetProcAddress(module, name));
  if (!function) {
    std::cerr << "FAIL: missing export " << name << '\n';
    ++failures;
  }
  return function;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: dll_api_tests <core-dll path>\n";
    return 2;
  }

  std::string dllPath = argv[1];
  auto const separator = dllPath.find_last_of("/\\");
  if (separator != std::string::npos) {
    SetDllDirectoryA(dllPath.substr(0, separator).c_str());
  }

  HMODULE module = LoadLibraryA(dllPath.c_str());
  if (!module) {
    std::cerr << "FAIL: LoadLibrary failed with " << GetLastError() << '\n';
    return 2;
  }

  using NoArg = int (*)();
  using Float1 = int (*)(float);
  using Float2 = int (*)(float, float);
  using Uint8 = int (*)(uint8_t);
  using Uint32 = int (*)(uint32_t);
  using Bool = int (*)(bool);
  using String = int (*)(char const*);
  using CreateRegular = int (*)(uint32_t, uint32_t, uint32_t, uint32_t);
  using CreateRectangle = int (*)(uint32_t, uint32_t, float, uint32_t);
  using CreateTorus = int (*)(uint32_t, uint32_t, float, float, uint32_t);
  using Animation = int (*)(uint32_t, float const*, int);
  using TransformInput = int (*)(uint32_t, uint32_t, uint32_t);
  using TransformValue = int (*)(uint32_t, uint32_t, float);
  using TransformFunction = int (*)(uint32_t, uint32_t, uint32_t, float);
  using TransformOperation = int (*)(uint32_t, uint32_t);

  auto createWorld = LoadFunction<Float1>(module, "create_world");
  auto destroyWorld = LoadFunction<NoArg>(module, "destroy_world");
  auto setWorldName = LoadFunction<String>(module, "set_world_name");
  auto serializeWorld = LoadFunction<String>(module, "serialize_world");
  auto createRegular = LoadFunction<CreateRegular>(module, "create_regular_polygon");
  auto createRectangle = LoadFunction<CreateRectangle>(module, "create_rectangle_polygon");
  auto createTorus = LoadFunction<CreateTorus>(module, "create_torus_polygon");
  auto setSize = LoadFunction<Float2>(module, "set_primitive_size");
  auto setLayer = LoadFunction<Uint8>(module, "set_primitive_layer");
  auto setPriority = LoadFunction<Uint8>(module, "set_primitive_priority");
  auto setFloor = LoadFunction<Float1>(module, "set_primitive_floor_z");
  auto setCeiling = LoadFunction<Float1>(module, "set_primitive_ceiling_z");
  auto setFlags = LoadFunction<Uint32>(module, "set_primitive_flags");
  auto addFlags = LoadFunction<Uint32>(module, "add_primitive_flags");
  auto removeFlags = LoadFunction<Uint32>(module, "remove_primitive_flags");
  auto setTimeDistance = LoadFunction<Float1>(module, "set_primitive_time_update_distance");
  auto setPosition = LoadFunction<Float2>(module, "set_primitive_position");
  auto setTransformOffset = LoadFunction<Float2>(module, "set_primitive_transform_offset");
  auto setEyeOffset = LoadFunction<Float2>(module, "set_primitive_influence_eye_origin_offset");
  auto setEyeAngle = LoadFunction<Float1>(module, "set_primitive_influence_eye_angle_offset");
  auto setFollowOrbit = LoadFunction<Bool>(module, "set_primitive_follow_orbit_angle");
  auto setAnimation = LoadFunction<Animation>(module, "set_primitive_animation_value");
  auto setTransformInput = LoadFunction<TransformInput>(module, "set_primitive_transform_0_input");
  auto setTransformConstant = LoadFunction<TransformValue>(module, "set_primitive_transform_0_constant");
  auto setTransformFunction = LoadFunction<TransformFunction>(module, "set_primitive_transform_0_function");
  auto setTransformOperation = LoadFunction<TransformOperation>(module, "set_primitive_transform_0_operation");

  if (failures != 0) {
    FreeLibrary(module);
    return 1;
  }

  Expect("initial destroy succeeds", destroyWorld() == 0);
  Expect("primitive creation without a world fails", createRegular(0, 0, 3, 0) != 0);
  Expect("world name without a world fails", setWorldName("world") != 0);
  Expect("serialization without a world fails", serializeWorld("unused.yaml") != 0);

  Expect("size without a primitive fails", setSize(1.0f, 1.0f) != 0);
  Expect("layer without a primitive fails", setLayer(0) != 0);
  Expect("priority without a primitive fails", setPriority(0) != 0);
  Expect("floor without a primitive fails", setFloor(0.0f) != 0);
  Expect("ceiling without a primitive fails", setCeiling(1.0f) != 0);
  Expect("flags without a primitive fail", setFlags(0) != 0);
  Expect("adding flags without a primitive fails", addFlags(1) != 0);
  Expect("removing flags without a primitive fails", removeFlags(1) != 0);
  Expect("time distance without a primitive fails", setTimeDistance(0.0f) != 0);
  Expect("position without a primitive fails", setPosition(0.0f, 0.0f) != 0);
  Expect("transform offset without a primitive fails", setTransformOffset(0.0f, 0.0f) != 0);
  Expect("eye offset without a primitive fails", setEyeOffset(0.0f, 0.0f) != 0);
  Expect("eye angle without a primitive fails", setEyeAngle(0.0f) != 0);
  Expect("follow orbit without a primitive fails", setFollowOrbit(false) != 0);
  Expect("animation without a primitive fails", setAnimation(0, nullptr, 0) != 0);
  Expect("transform input without a primitive fails", setTransformInput(0, 0, 0) != 0);
  Expect("transform constant without a primitive fails", setTransformConstant(0, 0, 0.0f) != 0);
  Expect("transform function without a primitive fails", setTransformFunction(0, 0, 2, 1.0f) != 0);
  Expect("transform operation without a primitive fails", setTransformOperation(0, 0) != 0);

  auto const nan = std::numeric_limits<float>::quiet_NaN();
  Expect("non-finite world size fails", createWorld(nan) != 0);
  Expect("undersized world fails", createWorld(511.0f) != 0);
  Expect("valid world succeeds", createWorld(512.0f) == 0);
  Expect("null world name fails", setWorldName(nullptr) != 0);
  Expect("null serialization filename fails", serializeWorld(nullptr) != 0);

  Expect("invalid operation fails", createRegular(UINT32_MAX, 0, 3, 0) != 0);
  Expect("invalid fill rule fails", createRegular(0, UINT32_MAX, 3, 0) != 0);
  Expect("too few sides fail", createRegular(0, 0, 2, 0) != 0);
  Expect("too many sides fail", createRegular(0, 0, 1025, 0) != 0);
  Expect("invalid material fails", createRegular(0, 0, 3, 2) != 0);
  Expect("invalid rectangle ratio fails", createRectangle(0, 0, 0.0f, 0) != 0);
  Expect("invalid torus count fails", createTorus(0, 0, 0.5f, 0.0f, 0) != 0);
  Expect("invalid torus material fails", createTorus(0, 0, 0.5f, 1.0f, 2) != 0);

  Expect("valid primitive succeeds", createRegular(0, 0, 3, 0) == 0);
  Expect("floor mutation reports success", setFloor(-1.0f) == 0);
  Expect("ceiling mutation reports success", setCeiling(2.0f) == 0);
  Expect("non-finite floor fails", setFloor(nan) != 0);
  Expect("non-finite size fails", setSize(nan, 1.0f) != 0);

  float validAnimation[] = {0.0f, 0.0f, 1.0f, 1.0f};
  Expect("null animation buffer fails", setAnimation(0, nullptr, 4) != 0);
  Expect("negative animation count fails", setAnimation(0, validAnimation, -2) != 0);
  Expect("odd animation count fails", setAnimation(0, validAnimation, 3) != 0);
  Expect("oversized animation count fails before reading", setAnimation(0, validAnimation, 34) != 0);
  Expect("invalid animation key fails", setAnimation(UINT32_MAX, validAnimation, 4) != 0);
  Expect("valid animation succeeds", setAnimation(0, validAnimation, 4) == 0);
  Expect("invalid transform key fails", setTransformConstant(UINT32_MAX, 0, 1.0f) != 0);
  Expect("invalid transform operand index fails", setTransformInput(0, 2, 0) != 0);
  Expect("invalid input enum fails", setTransformInput(0, 0, UINT32_MAX) != 0);
  Expect("invalid function enum fails", setTransformFunction(0, 0, UINT32_MAX, 1.0f) != 0);
  Expect("invalid operation enum fails", setTransformOperation(0, UINT32_MAX) != 0);

  Expect("replacement world succeeds", createWorld(1024.0f) == 0);
  Expect("replacement clears current primitive", setFloor(0.0f) != 0);
  Expect("primitive after replacement succeeds", createRectangle(0, 0, 1.0f, 0) == 0);
  Expect("destroy succeeds", destroyWorld() == 0);
  Expect("destroy clears current primitive", setCeiling(1.0f) != 0);
  Expect("repeated destroy succeeds", destroyWorld() == 0);

  FreeLibrary(module);
  return failures == 0 ? 0 : 1;
}
