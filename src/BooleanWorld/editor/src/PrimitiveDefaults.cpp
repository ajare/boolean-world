#include "Actions.h"
#include "AppHelpers.h"

#include <tuple>

#include <common/MaterialRegistry.h>

void editor::_setPrimitiveParameters(
    bw::core::Primitive* prim,
    uint8_t layer,
    uint8_t priority,
    wp::Vector2 const& position,
    wp::Vector2 const& offset,
    float scale,
    float angle) {
  using Key = bw::core::VertexTransformer::Key;

  prim->setLayer(layer);
  prim->setPriority(priority);
  prim->setSize(scale, scale);
  prim->setPosition(position);

  // Keep creation-time values in both current keyframes and their reset
  // defaults, matching the editor's ordinary ghost creation path.
  auto mutation = prim->mutate();
  mutation.animation(Key::Scale).setDefaultStructure({{0.0f, 1.0f}, {1.0f, 1.0f}}, {{bw::core::Easing::Linear}}, true);
  mutation.animation(Key::Angle).setDefaultStructure({{0.0f, angle}, {1.0f, angle}}, {{bw::core::Easing::Linear}}, true);
  mutation.animation(Key::OrbitAngle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
  mutation.animation(Key::OrbitDistance).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
}

void editor::setPrimitiveDefaultMaterial(
    uint32_t materialIndex,
    bw::core::MaterialDefinitionData* materialDefinition) {
  if (materialIndex >= bw::common::MaterialNames.size()) {
    return;
  }

  auto const& material = bw::common::MaterialNames[materialIndex];
  auto numParams = static_cast<uint32_t>(std::get<1>(material));
  for (uint32_t i = 0; i < numParams; ++i) {
    materialDefinition->params[i] =
        std::get<3>(bw::common::MaterialParams[materialIndex][i]);
  }

  auto const& defaultColour = std::get<2>(material);
  for (uint32_t i = 0; i < 3; ++i) {
    materialDefinition->baseColour[i] = defaultColour[i];
  }
}

void editor::setPrimitiveDefaultMaterials(bw::core::Primitive* prim) {
  auto properties = prim->getProperties();
  setPrimitiveDefaultMaterial(
      properties.floorMaterialIndex, &properties.floorMaterialDef.data);
  setPrimitiveDefaultMaterial(
      properties.ceilingMaterialIndex, &properties.ceilingMaterialDef.data);
  setPrimitiveDefaultMaterial(
      properties.wallMaterialIndex, &properties.wallMaterialDef.data);
  prim->setProperties(properties);
}
