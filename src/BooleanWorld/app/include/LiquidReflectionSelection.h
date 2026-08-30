#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace bw::app {

inline constexpr float liquidElevationGroupingTolerance = 0.01f;
inline constexpr float liquidReflectionChallengerCoverageRatio = 1.2f;
inline constexpr float liquidReflectionSideHysteresis = 0.05f;
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

// Stateful selection policy. Visible selections keep their slots until a
// challenger has at least 20% more projected coverage. Their viewer side also
// remains stable until the camera is 0.05 world units through the plane.
class LiquidReflectionSelectionPolicy {
public:
  [[nodiscard]] std::vector<SelectedLiquidSurface> select(
      std::span<LiquidSurfaceTriangle const> triangles,
      glm::mat4 const& viewProjection,
      glm::vec3 const& cameraPosition);

  void reset();

private:
  std::vector<SelectedLiquidSurface> mSelected;
};

// Stateless convenience used where selection history is deliberately absent.
// Groups frustum-visible elevations, ranks them by coverage, distance, then
// elevation, and returns at most four candidates.
[[nodiscard]] std::vector<SelectedLiquidSurface> selectLiquidSurfaces(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition);

}  // namespace bw::app
