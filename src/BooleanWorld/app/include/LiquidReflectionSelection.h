#pragma once

#include <array>
#include <optional>
#include <span>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace bw::app {

// One generated Liquid surface triangle in renderer world space.
struct LiquidSurfaceTriangle {
  std::array<glm::vec3, 3> vertices;
  float elevation;
};

struct DominantLiquidSurface {
  float elevation;
  float projectedCoverage;
  float cameraDistance;
  bool viewerAbove;
};

// Selects the frustum-visible Liquid elevation with the greatest projected
// coverage. Elevations within 0.01 world units share their coverage; distance
// and then lower elevation provide deterministic tie-breakers.
[[nodiscard]] std::optional<DominantLiquidSurface>
selectDominantLiquidSurface(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition);

}  // namespace bw::app
