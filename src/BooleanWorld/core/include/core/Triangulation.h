#pragma once

#define NOMINMAX

#include <algorithm>
#include <vector>
#include <array>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/MathsUtils.h>

#include "core/Platform.h"
#include "core/Vertex.h"

namespace bw {
namespace core {

struct Triangulation {
  struct Triangle {
    std::array<Vertex, 3> v;
    wp::BoundingBox bounds;
    uint32_t primitiveIndex;

    void getBarycentricCoords(wp::Vector2 const& pos, float& bu, float& bv, float& bw) const {
      wp::MathsUtils::barycentricCoords(pos, v[0].p, v[1].p, v[2].p, bu, bv, bw);
    }
  };

  std::vector<Triangle> tris;
  wp::BoundingBox bounds;

private:
  void copyFrom(Triangulation const& other) {
    tris = other.tris;
    bounds = other.bounds;
  }

  void moveFrom(Triangulation& other) {
    tris = std::move(other.tris);
    bounds = other.bounds;
  }

public:
  Triangulation() = default;

  Triangulation(Triangulation const& other) noexcept {
    copyFrom(other);
  }

  Triangulation& operator=(Triangulation const& other) noexcept {
    copyFrom(other);

    return *this;
  }

  Triangulation(Triangulation&& other) noexcept {
    moveFrom(other);
  }

  Triangulation& operator=(Triangulation&& other) noexcept {
    moveFrom(other);

    return *this;
  }

  void calculateBounds() {
    wp::Vector2 minExtent, maxExtent;
    bounds.getExtents(minExtent, maxExtent);

    for (auto const& tri : tris) {
      for (auto const& vertex : tri.v) {
        minExtent.x = minExtent.x < vertex.p.x ? minExtent.x : vertex.p.x;
        minExtent.y = minExtent.y < vertex.p.y ? minExtent.y : vertex.p.y;
        maxExtent.x = maxExtent.x > vertex.p.x ? maxExtent.x : vertex.p.x;
        maxExtent.y = maxExtent.y > vertex.p.y ? maxExtent.y : vertex.p.y;
      }
    }

    bounds.setPosition(minExtent);
    bounds.setSize(maxExtent - minExtent);
  }

  void merge(Triangulation const& other, bool calcBounds) {
    std::copy(other.tris.begin(), other.tris.end(), std::back_inserter(tris));

    if (calcBounds) {
      calculateBounds();
    }
  }

  bool pointInside(wp::Vector2 const& pos) const {
    if (!bounds.pointInside(pos)) {
      return false;
    }

    for (auto const& tri : tris) {
      if (wp::MathsUtils::pointInTriangle(pos, tri.v[0].p, tri.v[1].p, tri.v[2].p)) {
        return true;
      }
    }

    return false;
  }

  int32_t getContainingTriangleIndex(wp::Vector2 const& pos) const {
    if (!bounds.pointInside(pos)) {
      return -1;
    }

    for (int32_t i = 0; i < (int32_t)tris.size(); ++i) {
      auto const& tri = tris[i];

      if (wp::MathsUtils::pointInTriangle(pos, tri.v[0].p, tri.v[1].p, tri.v[2].p)) {
        return i;
      }
    }

    return -1;
  }
};

}  // namespace core
}  // namespace bw
