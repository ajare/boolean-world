#include "Actions.h"
#include "AppHelpers.h"

void editor::_setPrimitiveParameters(
    bw::core::Primitive* prim,
    uint8_t priority,
    wp::Vector2 const& position,
    wp::Vector2 const& offset,
    float scale,
    float angle) {
  using Key = bw::core::VertexTransformer::Key;

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

void editor::setPrimitiveDefaultMaterials(bw::core::Primitive* prim) {
  auto properties = prim->getProperties();
  properties.floorMaterialId = "builtin.marble";
  properties.ceilingMaterialId = "builtin.marble";
  properties.wallMaterialId = "builtin.marble";
  prim->setProperties(properties);
}
