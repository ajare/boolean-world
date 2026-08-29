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
  auto selected = bw::app::selectDominantLiquidSurface(
      surfaces, overheadProjection(), {0.0f, 5.0f, 0.0f});
  require(!selected, "an out-of-frustum Liquid triangle was selected");
}

void groupsElevationsAndSelectsDominantCoverage() {
  std::vector surfaces{
      triangle(0.0f, {-0.9f, -0.9f}, {-0.3f, -0.9f}, {-0.9f, -0.3f}),
      triangle(0.005f, {-0.2f, -0.9f}, {0.4f, -0.9f}, {-0.2f, -0.3f}),
      triangle(0.5f, {0.2f, 0.2f}, {0.7f, 0.2f}, {0.2f, 0.7f})};
  auto selected = bw::app::selectDominantLiquidSurface(
      surfaces, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(selected.has_value(), "visible Liquid produced no reflection plane");
  require(selected->elevation > 0.0f && selected->elevation < 0.0051f,
          "near-equal Liquid elevations did not share projected coverage");
  require(selected->viewerAbove,
          "the selected plane did not classify the viewer side");
}

void breaksCoverageTiesByDistanceThenElevation() {
  std::vector surfaces{
      triangle(-0.5f, {-0.8f, 0.1f}, {-0.4f, 0.1f}, {-0.8f, 0.5f}),
      triangle(0.5f, {0.4f, 0.1f}, {0.8f, 0.1f}, {0.8f, 0.5f})};
  auto selected = bw::app::selectDominantLiquidSurface(
      surfaces, overheadProjection(), {0.0f, 0.0f, 0.3f});
  require(selected && selected->elevation == -0.5f,
          "equal Liquid candidates did not use the deterministic elevation tie-break");
  require(selected && selected->viewerAbove,
          "viewer-side classification was incorrect below the camera");
}
}  // namespace

int main() {
  try {
    rejectsTrianglesOutsideTheFrustum();
    groupsElevationsAndSelectsDominantCoverage();
    breaksCoverageTiesByDistanceThenElevation();
    std::cout << "Liquid reflection selection tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
