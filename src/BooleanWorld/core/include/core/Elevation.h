#pragma once

#include <array>
#include <cmath>

#include <willpower/common/Vector2.h>

namespace bw::core {

// Authored floor/ceiling values. directionAngle is measured counter-clockwise
// from local +Y; lower and upper apply at the fitted OBB's opposite edges.
struct ElevationSpan {
  float directionAngle{0.0f};
  float lowerElevation{0.0f};
  float upperElevation{0.0f};

  friend bool operator==(ElevationSpan const&, ElevationSpan const&) = default;
};

struct ElevationBounds {
  std::array<wp::Vector2, 4> corners{};
  wp::Vector2 direction{0.0f, 1.0f};
  float minimumDirection{};
  float maximumDirection{};
};

enum class PrimitiveSurface { Floor, Ceiling };

// An affine elevation plane over the World plane. The gradient components are
// elevation gained per World unit along +X and +Y; baseElevation is the
// elevation at the World-plane origin.
struct Elevation {
  float baseElevation{0.0f};
  wp::Vector2 gradient{};

  Elevation() = default;
  Elevation(float baseElevation_) : baseElevation(baseElevation_) {}
  Elevation(float baseElevation_, wp::Vector2 const& gradient_)
      : baseElevation(baseElevation_), gradient(gradient_) {}

  Elevation& operator=(float elevation) {
    baseElevation = elevation;
    gradient = {};
    return *this;
  }

  // Retains source compatibility for flat-world consumers while the
  // position-based APIs are adopted. It denotes the elevation at the origin,
  // and must not be used to sample a non-horizontal plane.
  operator float() const { return baseElevation; }

  Elevation& operator+=(float offset) {
    baseElevation += offset;
    return *this;
  }

  Elevation& operator-=(float offset) {
    baseElevation -= offset;
    return *this;
  }

  [[nodiscard]] float evaluate(wp::Vector2 const& position) const {
    return baseElevation + gradient.dot(position);
  }

  [[nodiscard]] float getElevation(wp::Vector2 const& position) const {
    return evaluate(position);
  }

  // Unit normal pointing toward increasing elevation-axis values. Floors use
  // this direction; a ceiling's outward geometric normal is its opposite.
  [[nodiscard]] std::array<float, 3> normal() const {
    auto inverseLength =
        1.0f / std::sqrt(gradient.lengthSquared() + 1.0f);
    return {
        -gradient.x * inverseLength,
        -gradient.y * inverseLength,
        inverseLength};
  }

  [[nodiscard]] std::array<float, 3> getNormal() const { return normal(); }

  friend bool operator==(Elevation const& lhs, Elevation const& rhs) {
    return lhs.baseElevation == rhs.baseElevation &&
           lhs.gradient == rhs.gradient;
  }

  friend bool operator==(Elevation const& lhs, float rhs) {
    return lhs.baseElevation == rhs;
  }

  friend bool operator==(float lhs, Elevation const& rhs) {
    return lhs == rhs.baseElevation;
  }
};

}  // namespace bw::core
