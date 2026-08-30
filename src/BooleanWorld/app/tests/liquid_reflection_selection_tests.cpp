#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include "LiquidReflectionSelection.h"

namespace {
void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 0.00001f) {
  return std::abs(left - right) <= tolerance;
}

glm::mat4 overheadProjection() {
  glm::mat4 result{1.0f};
  result[1][1] = 0.0f;
  result[2][2] = 0.0f;
  result[2][1] = 1.0f;
  result[1][2] = 1.0f;
  return result;
}

bw::app::LiquidSurfaceTriangle triangle(
    float elevation, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
  bw::app::LiquidSurfaceTriangle result;
  result.vertices = {
      glm::vec3(a.x, elevation, a.y),
      glm::vec3(b.x, elevation, b.y),
      glm::vec3(c.x, elevation, c.y)};
  result.elevation = elevation;
  return result;
}

void rejectsTrianglesOutsideTheFrustum() {
  std::vector surfaces{
      triangle(2.0f, {2.0f, 0.0f}, {3.0f, 0.0f}, {2.0f, 1.0f})};
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.0f, 5.0f, 0.0f});
  require(selected.empty(),
          "an out-of-frustum Liquid triangle consumed a Planar slot");
}

void groupsTheInclusiveToleranceBoundary() {
  std::vector surfaces{
      triangle(0.0f, {-0.9f, -0.9f}, {-0.5f, -0.9f}, {-0.9f, -0.5f}),
      triangle(0.01f, {-0.4f, -0.9f}, {0.0f, -0.9f}, {-0.4f, -0.5f}),
      triangle(0.0101f, {0.1f, -0.9f}, {0.5f, -0.9f}, {0.1f, -0.5f})};
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(selected.size() == 2,
          "the 0.01 Liquid elevation grouping boundary is incorrect");
  auto const& grouped = selected[0].minimumElevation == 0.0f
                            ? selected[0]
                            : selected[1];
  require(near(grouped.minimumElevation, 0.0f) &&
              near(grouped.maximumElevation, 0.01f) &&
              grouped.elevation > 0.0f && grouped.elevation < 0.01f,
          "near-equal Liquid elevations did not retain one matching group");
}

void ranksByCoverageThenDistance() {
  std::vector surfaces{
      triangle(0.0f, {-0.9f, -0.9f}, {0.0f, -0.9f}, {-0.9f, 0.0f}),
      triangle(0.5f, {0.2f, 0.2f}, {0.6f, 0.2f}, {0.2f, 0.6f}),
      triangle(1.0f, {0.2f, -0.8f}, {0.6f, -0.8f}, {0.2f, -0.4f})};
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.2f, 0.5f, 0.4f});
  require(selected.size() == 3 && near(selected[0].elevation, 0.0f),
          "projected coverage was not the primary Planar ranking key");
  require(near(selected[1].elevation, 0.5f) &&
              near(selected[2].elevation, 1.0f),
          "camera distance did not order equal-coverage candidates");
}

void breaksExactTiesByLowestElevation() {
  std::vector surfaces{
      triangle(-0.5f, {-0.8f, 0.1f}, {-0.4f, 0.1f}, {-0.8f, 0.5f}),
      triangle(0.5f, {0.4f, 0.1f}, {0.8f, 0.1f}, {0.8f, 0.5f})};
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.0f, 0.0f, 0.3f});
  require(selected.size() == 2 && near(selected[0].elevation, -0.5f),
          "equal Liquid candidates did not use lowest elevation as the deterministic tie-break");
  require(selected[0].viewerAbove && !selected[1].viewerAbove,
          "selected planes did not classify their viewer sides");
}

void capsSelectionAtFourRankedCandidates() {
  std::vector<bw::app::LiquidSurfaceTriangle> surfaces;
  for (int index = 0; index < 5; ++index) {
    auto x = -0.9f + 0.35f * static_cast<float>(index);
    surfaces.push_back(triangle(
        0.1f * static_cast<float>(index), {x, -0.2f}, {x + 0.25f, -0.2f},
        {x, 0.05f}));
  }
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.0f, 0.0f, 0.0f});
  require(selected.size() == bw::app::maximumPlanarLiquidSurfaces,
          "more than four Liquid elevations consumed Planar slots");
}
}  // namespace

int main() {
  try {
    rejectsTrianglesOutsideTheFrustum();
    groupsTheInclusiveToleranceBoundary();
    ranksByCoverageThenDistance();
    breaksExactTiesByLowestElevation();
    capsSelectionAtFourRankedCandidates();
    std::cout << "Liquid reflection selection tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
