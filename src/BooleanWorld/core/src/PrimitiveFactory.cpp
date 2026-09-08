#include "core/PrimitiveFactory.h"

#include <type_traits>

#include "core/CirclePolygon.h"
#include "core/CircleSegmentPolygon.h"
#include "core/RectanglePolygon.h"
#include "core/RegularPolygon.h"
#include "core/SuperformulaPolygon.h"
#include "core/TorusPolygon.h"
#include "core/TorusSegmentPolygon.h"

namespace bw::core {
namespace {

template <class>
inline constexpr bool alwaysFalse = false;

std::unique_ptr<Primitive> createShape(PrimitiveSpec const& spec) {
  return std::visit(
      [&](auto const& shape) -> std::unique_ptr<Primitive> {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, RegularPolygonSpec>) {
          return std::make_unique<RegularPolygon>(spec.operation, spec.fillRule, shape.numSides);
        } else if constexpr (std::is_same_v<Shape, CircleSpec>) {
          return std::make_unique<CirclePolygon>(spec.operation, spec.fillRule, shape.resolution);
        } else if constexpr (std::is_same_v<Shape, CircleSegmentSpec>) {
          return std::make_unique<CircleSegmentPolygon>(spec.operation, spec.fillRule, shape.arcLength, shape.resolution);
        } else if constexpr (std::is_same_v<Shape, TorusSpec>) {
          return std::make_unique<TorusPolygon>(spec.operation, spec.fillRule, shape.thickness, shape.resolution);
        } else if constexpr (std::is_same_v<Shape, TorusSegmentSpec>) {
          return std::make_unique<TorusSegmentPolygon>(spec.operation, spec.fillRule, shape.thickness, shape.arcLength, shape.resolution);
        } else if constexpr (std::is_same_v<Shape, RectangleSpec>) {
          return std::make_unique<RectanglePolygon>(spec.operation, spec.fillRule, shape.xyRatio);
        } else if constexpr (std::is_same_v<Shape, SuperformulaSpec>) {
          auto values = shape.values;
          return std::make_unique<SuperformulaPolygon>(spec.operation, spec.fillRule, shape.resolution, values.data());
        } else {
          static_assert(alwaysFalse<Shape>, "Unsupported Primitive shape specification");
        }
      },
      spec.shape);
}

}  // namespace

std::unique_ptr<Primitive> PrimitiveFactory::create(PrimitiveSpec const& spec) {
  auto primitive = createShape(spec);
  primitive->setPriority(spec.priority);
  primitive->setSize(spec.scale, spec.scale);
  primitive->setPosition(spec.position);

  // Keep creation-time values in both current keyframes and their reset
  // defaults, matching authored Primitive creation in the editor.
  using Key = VertexTransformer::Key;
  auto mutation = primitive->mutate();
  mutation.animation(Key::Scale).setDefaultStructure({{0.0f, 1.0f}, {1.0f, 1.0f}}, {{Easing::Linear}}, true);
  mutation.animation(Key::Angle).setDefaultStructure({{0.0f, spec.angle}, {1.0f, spec.angle}}, {{Easing::Linear}}, true);
  mutation.animation(Key::OrbitAngle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{Easing::Linear}}, true);
  mutation.animation(Key::OrbitDistance).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{Easing::Linear}}, true);
  return primitive;
}

}  // namespace bw::core
