#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "module/Module.h"

#include "core/Defines.h"
#include "common/MaterialRegistry.h"

#include "core/InputType.h"
#include "core/RectanglePolygon.h"
#include "core/RegularPolygon.h"
#include "core/TorusPolygon.h"
#include "core/World.h"
#include "core/YamlSerializer.h"
#include "core/tTransform.h"

namespace {
using namespace bw::core;

World* gWorld{nullptr};
Primitive* gPrimitive{nullptr};

template <typename Function>
int InvokeApi(Function&& function) {
  try {
    return std::forward<Function>(function)();
  } catch (std::exception const& exception) {
    std::fprintf(stderr, "ERROR: %s\n", exception.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "ERROR: unknown exception\n");
    return 1;
  }
}

bool IsFinite(float value) {
  return std::isfinite(value);
}

bool IsValidPrimitiveType(uint32_t operation, uint32_t fillType) {
  return operation <= static_cast<uint32_t>(Primitive::Operation::XOR) &&
         fillType <= static_cast<uint32_t>(Primitive::FillRule::EvenOdd);
}

bool IsValidMaterialIndex(uint32_t materialIndex) {
  return materialIndex < bw::common::MaterialNames.size();
}

bool IsValidTransformKey(uint32_t key) {
  return key < static_cast<uint32_t>(VertexTransformer::Key::COUNT);
}

void SetPrimitiveMaterialDefault(uint32_t materialIndex, MaterialDefinitionData* materialDefinition) {
  auto const& material = bw::common::MaterialNames[materialIndex];
  auto const numParams = static_cast<uint32_t>(std::get<1>(material));

  for (uint32_t i = 0; i < numParams; ++i) {
    materialDefinition->params[i] = std::get<3>(bw::common::MaterialParams[materialIndex][i]);
  }

  auto const& defaultColour = std::get<2>(material);
  for (uint32_t i = 0; i < defaultColour.size(); ++i) {
    materialDefinition->baseColour[i] = defaultColour[i];
  }
}

void SetPrimitiveMaterials(Primitive* primitive, uint32_t materialIndex) {
  auto properties = primitive->getProperties();
  properties.floorMaterialIndex = materialIndex;
  properties.ceilingMaterialIndex = materialIndex;
  properties.wallMaterialIndex = materialIndex;

  SetPrimitiveMaterialDefault(materialIndex, &properties.floorMaterialDef.data);
  SetPrimitiveMaterialDefault(materialIndex, &properties.ceilingMaterialDef.data);
  SetPrimitiveMaterialDefault(materialIndex, &properties.wallMaterialDef.data);
  primitive->setProperties(properties);
}

template <typename Factory>
int CreatePrimitive(uint32_t operation, uint32_t fillType, uint32_t materialIndex, Factory&& factory) {
  if (!gWorld || !IsValidPrimitiveType(operation, fillType) || !IsValidMaterialIndex(materialIndex)) {
    return 1;
  }

  auto primitive = std::forward<Factory>(factory)(
      static_cast<Primitive::Operation>(operation),
      static_cast<Primitive::FillRule>(fillType));
  SetPrimitiveMaterials(primitive.get(), materialIndex);
  gWorld->addPrimitive(primitive.get());
  gPrimitive = primitive.release();
  return 0;
}

bool HasCurrentPrimitive() {
  return gWorld != nullptr && gPrimitive != nullptr;
}
}  // namespace

extern "C" {
int mod_create_world(float size) {
  return InvokeApi([&]() {
    if (!IsFinite(size) || size < 512.0f) {
      return 1;
    }

    auto replacement = std::make_unique<World>(size, size / 16.0f);
    gPrimitive = nullptr;
    delete std::exchange(gWorld, nullptr);
    gWorld = replacement.release();
    return 0;
  });
}

int mod_destroy_world() {
  return InvokeApi([]() {
    gPrimitive = nullptr;
    delete std::exchange(gWorld, nullptr);
    return 0;
  });
}

int mod_set_world_name(char const* name) {
  return InvokeApi([&]() {
    if (!gWorld || !name) {
      return 1;
    }
    gWorld->setName(name);
    return 0;
  });
}

int mod_serialize_world(char const* filename) {
  return InvokeApi([&]() {
    if (!gWorld || !filename || filename[0] == '\0') {
      return 1;
    }

    auto serializer = std::shared_ptr<YamlSerializer>(YamlSerializer::toFile(filename));
    auto workData = SerializationWorkData{};
    gWorld->serialize(serializer, workData);
    serializer->serialize();
    return 0;
  });
}

int mod_create_regular_polygon(uint32_t operation, uint32_t fillType, uint32_t numSides, uint32_t materialIndex) {
  return InvokeApi([&]() {
    if (numSides < 3 || numSides > BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
      return 1;
    }
    return CreatePrimitive(operation, fillType, materialIndex, [&](Primitive::Operation validOperation, Primitive::FillRule validFillType) {
      return std::make_unique<RegularPolygon>(validOperation, validFillType, numSides);
    });
  });
}

int mod_create_rectangle_polygon(uint32_t operation, uint32_t fillType, float xyRatio, uint32_t materialIndex) {
  return InvokeApi([&]() {
    if (!IsFinite(xyRatio) || xyRatio <= 0.0f) {
      return 1;
    }
    return CreatePrimitive(operation, fillType, materialIndex, [&](Primitive::Operation validOperation, Primitive::FillRule validFillType) {
      return std::make_unique<RectanglePolygon>(validOperation, validFillType, xyRatio);
    });
  });
}

int mod_create_torus_polygon(uint32_t operation, uint32_t fillType, float thickness, float resolution, uint32_t materialIndex) {
  return InvokeApi([&]() {
    if (!IsFinite(thickness) || thickness < 0.0f || thickness > 1.0f ||
        !IsFinite(resolution) || resolution < 3.0f / 64.0f ||
        resolution > static_cast<float>(BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) / 64.0f) {
      return 1;
    }
    return CreatePrimitive(operation, fillType, materialIndex, [&](Primitive::Operation validOperation, Primitive::FillRule validFillType) {
      return std::make_unique<TorusPolygon>(validOperation, validFillType, thickness, resolution);
    });
  });
}

int mod_set_primitive_size(float width, float height) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(width) || !IsFinite(height) || width <= 0.0f || height <= 0.0f) {
      return 1;
    }
    gPrimitive->setSize(wp::Vector2(width, height));
    return 0;
  });
}

int mod_set_primitive_priority(uint8_t priority) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive()) {
      return 1;
    }
    gPrimitive->setPriority(priority);
    return 0;
  });
}

int mod_set_primitive_floor_z(float z) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(z)) {
      return 1;
    }
    auto properties = gPrimitive->getProperties();
    properties.floorZ = z;
    gPrimitive->setProperties(properties);
    return 0;
  });
}

int mod_set_primitive_ceiling_z(float z) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(z)) {
      return 1;
    }
    auto properties = gPrimitive->getProperties();
    properties.ceilingZ = z;
    gPrimitive->setProperties(properties);
    return 0;
  });
}

int mod_set_primitive_flags(uint32_t flags) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive()) {
      return 1;
    }
    gPrimitive->setFlags(flags);
    return 0;
  });
}

int mod_add_primitive_flags(uint32_t flags) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive()) {
      return 1;
    }
    gPrimitive->setFlags(gPrimitive->getFlags() | flags);
    return 0;
  });
}

int mod_remove_primitive_flags(uint32_t flags) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive()) {
      return 1;
    }
    gPrimitive->setFlags(gPrimitive->getFlags() & ~flags);
    return 0;
  });
}

int mod_set_primitive_time_update_distance(float distance) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(distance) || distance < 0.0f) {
      return 1;
    }
    gPrimitive->setTimeUpdateDistance(distance);
    return 0;
  });
}

int mod_set_primitive_position(float x, float y) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(x) || !IsFinite(y)) {
      return 1;
    }
    gPrimitive->setPosition(wp::Vector2(x, y));
    return 0;
  });
}

int mod_set_primitive_transform_offset(float x, float y) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(x) || !IsFinite(y)) {
      return 1;
    }
    gPrimitive->setTransformOffset(wp::Vector2(x, y));
    return 0;
  });
}

int mod_set_primitive_influence_eye_origin_offset(float x, float y) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(x) || !IsFinite(y)) {
      return 1;
    }
    gPrimitive->setInfluenceEyeOriginOffset(wp::Vector2(x, y));
    return 0;
  });
}

int mod_set_primitive_influence_eye_angle_offset(float angle) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsFinite(angle)) {
      return 1;
    }
    gPrimitive->setInfluenceEyeAngleOffset(angle);
    return 0;
  });
}

int mod_set_primitive_follow_orbit_angle(bool follow) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive()) {
      return 1;
    }
    gPrimitive->setFollowOrbitAngle(follow);
    return 0;
  });
}

int mod_set_primitive_transform_0_input(uint32_t key, uint32_t index, uint32_t inputType) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || index >= 2 ||
        inputType >= static_cast<uint32_t>(InputType::COUNT)) {
      return 1;
    }
    gPrimitive->setTransformOperand(static_cast<VertexTransformer::Key>(key), 0, index, tTransform::OperandType::Input);
    gPrimitive->setTransformInput(static_cast<VertexTransformer::Key>(key), 0, index, static_cast<InputType>(inputType));
    return 0;
  });
}

int mod_set_primitive_transform_0_constant(uint32_t key, uint32_t index, float value) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || index >= 2 || !IsFinite(value)) {
      return 1;
    }
    gPrimitive->setTransformOperand(static_cast<VertexTransformer::Key>(key), 0, index, tTransform::OperandType::Constant);
    gPrimitive->setTransformConstant(static_cast<VertexTransformer::Key>(key), 0, index, value);
    return 0;
  });
}

int mod_set_primitive_transform_0_previous(uint32_t key, uint32_t index) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || index >= 2) {
      return 1;
    }
    gPrimitive->setTransformOperand(static_cast<VertexTransformer::Key>(key), 0, index, tTransform::OperandType::TransformOutput);
    return 0;
  });
}

int mod_set_primitive_transform_0_function(uint32_t key, uint32_t index, uint32_t fn, float value) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || index >= 2 || !IsFinite(value) ||
        fn < static_cast<uint32_t>(tTransform::OperandType::Sine) ||
        fn > static_cast<uint32_t>(tTransform::OperandType::Square)) {
      return 1;
    }
    gPrimitive->setTransformOperand(static_cast<VertexTransformer::Key>(key), 0, index, static_cast<tTransform::OperandType>(fn));
    gPrimitive->setTransformFnMultiplier(static_cast<VertexTransformer::Key>(key), 0, index, value);
    return 0;
  });
}

int mod_set_primitive_transform_0_operation(uint32_t key, uint32_t op) {
  return InvokeApi([&]() {
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || op >= static_cast<uint32_t>(tTransform::Operation::COUNT)) {
      return 1;
    }
    gPrimitive->setTransformOperation(static_cast<VertexTransformer::Key>(key), 0, static_cast<tTransform::Operation>(op));
    return 0;
  });
}

int mod_set_primitive_animation_value(uint32_t key, float const* values, int numValues) {
  return InvokeApi([&]() {
    constexpr int MaxAnimationValues = 2 * static_cast<int>(Interpolator<float>::MaxPoints);
    if (!HasCurrentPrimitive() || !IsValidTransformKey(key) || numValues < 4 ||
        numValues > MaxAnimationValues || numValues % 2 != 0 || !values) {
      return 1;
    }

    std::vector<std::pair<float, float>> animationValues;
    animationValues.reserve(static_cast<size_t>(numValues / 2));
    for (int i = 0; i < numValues; i += 2) {
      if (!IsFinite(values[i]) || !IsFinite(values[i + 1]) ||
          (i > 0 && values[i] < values[i - 2])) {
        return 1;
      }
      animationValues.emplace_back(values[i], values[i + 1]);
    }

    auto mutation = gPrimitive->mutate();
    mutation.animation(static_cast<VertexTransformer::Key>(key)).setPoints(animationValues);
    return 0;
  });
}
}  // extern "C"
