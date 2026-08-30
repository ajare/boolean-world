#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace bw::app {

inline constexpr float liquidElevationGroupingTolerance = 0.01f;
inline constexpr std::size_t maximumPlanarLiquidSurfaces = 4;

// One generated Liquid surface triangle in renderer world space.
struct LiquidSurfaceTriangle {
  std::array<glm::vec3, 3> vertices;
  float elevation;
};

// One ranked group of nearly equal Liquid surface elevations. The inclusive
// bounds let Water fragments identify their group without assigning an
// unselected neighbouring elevation to this reflection image.
struct SelectedLiquidSurface {
  float elevation;
  float minimumElevation;
  float maximumElevation;
  float projectedCoverage;
  float cameraDistance;
  bool viewerAbove;
};

// Groups frustum-visible Liquid elevations within 0.01 world units, ranks the
// groups by projected coverage, camera distance, then lowest elevation, and
// returns at most four candidates in rank order.
[[nodiscard]] std::vector<SelectedLiquidSurface> selectLiquidSurfaces(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition);

}  // namespace bw::app
