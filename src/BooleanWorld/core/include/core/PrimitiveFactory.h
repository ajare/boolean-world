#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <variant>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"
#include "core/Primitive.h"

namespace bw::core {

struct RegularPolygonSpec {
  uint32_t numSides{3};
};

struct CircleSpec {
  float resolution{0.5f};
};

struct CircleSegmentSpec {
  float arcLength{90.0f};
  float resolution{0.5f};
};

struct TorusSpec {
  float thickness{0.5f};
  float resolution{0.5f};
};

struct TorusSegmentSpec {
  float thickness{0.5f};
  float arcLength{90.0f};
  float resolution{0.5f};
};

struct RectangleSpec {
  float xyRatio{1.0f};
};

struct SuperformulaSpec {
  std::array<float, 6> values{1.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  float resolution{0.5f};
};

using PrimitiveShapeSpec = std::variant<
    RegularPolygonSpec,
    CircleSpec,
    CircleSegmentSpec,
    TorusSpec,
    TorusSegmentSpec,
    RectangleSpec,
    SuperformulaSpec>;

// Complete authored state needed to create one procedural Primitive.
struct PrimitiveSpec {
  PrimitiveShapeSpec shape;
  Primitive::Operation operation{Primitive::Operation::Union};
  Primitive::FillRule fillRule{Primitive::FillRule::NonZero};
  uint8_t priority{0};
  wp::Vector2 position{wp::Vector2::ZERO};
  float scale{1.0f};
  float angle{0.0f};
};

class BW_API PrimitiveFactory final {
public:
  [[nodiscard]] static std::unique_ptr<Primitive> create(PrimitiveSpec const& spec);
};

}  // namespace bw::core
