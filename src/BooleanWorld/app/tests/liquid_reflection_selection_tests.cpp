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

std::vector<bw::app::LiquidSurfaceTriangle> competingSurfaces(
    float finalSize, bool omitSecond = false) {
  std::vector<bw::app::LiquidSurfaceTriangle> surfaces;
  for (int index = 0; index < 5; ++index) {
    if (omitSecond && index == 1) continue;
    auto size = index == 4 ? finalSize : 0.4f;
    surfaces.push_back(triangle(
        0.1f * static_cast<float>(index), {-size, -size}, {size, -size},
        {-size, size}));
  }
  return surfaces;
}

void capsSelectionAtFourRankedCandidates() {
  auto surfaces = competingSurfaces(0.35f);
  auto selected = bw::app::selectLiquidSurfaces(
      surfaces, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(selected.size() == bw::app::maximumPlanarLiquidSurfaces,
          "more than four Liquid elevations consumed Planar slots");
}

void retainsSlotsUntilAChallengerClearsTheThreshold() {
  bw::app::LiquidReflectionSelectionPolicy policy;
  auto initial = competingSurfaces(0.35f);
  auto selected =
      policy.select(initial, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(selected.size() == 4 && near(selected[0].elevation, 0.3f) &&
              near(selected[1].elevation, 0.2f) &&
              near(selected[2].elevation, 0.1f) &&
              near(selected[3].elevation, 0.0f),
          "initial Planar candidates were not assigned by deterministic rank");

  auto insufficient = competingSurfaces(0.4f * std::sqrt(1.19f));
  selected = policy.select(
      insufficient, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(near(selected[3].elevation, 0.0f),
          "an under-threshold challenger displaced a selected Planar plane");

  auto sufficient = competingSurfaces(0.4f * std::sqrt(1.21f));
  selected = policy.select(
      sufficient, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(near(selected[3].elevation, 0.4f) &&
              near(selected[0].elevation, 0.3f) &&
              near(selected[1].elevation, 0.2f) &&
              near(selected[2].elevation, 0.1f),
          "a qualifying challenger did not take only the weakest plane's slot");
}

void reusesTheDepartedPlanesSlot() {
  bw::app::LiquidReflectionSelectionPolicy policy;
  auto initial = competingSurfaces(0.35f);
  auto selected =
      policy.select(initial, overheadProjection(), {0.0f, 2.0f, 0.0f});
  auto afterDeparture = competingSurfaces(0.35f, true);
  selected = policy.select(
      afterDeparture, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(selected.size() == 4 && near(selected[0].elevation, 0.3f) &&
              near(selected[1].elevation, 0.2f) &&
              near(selected[2].elevation, 0.4f) &&
              near(selected[3].elevation, 0.0f),
          "a frustum departure churned surviving slots or left its slot unused");
}

void retainsAStableDescriptorForAVisiblePlane() {
  bw::app::LiquidReflectionSelectionPolicy policy;
  std::vector initial{
      triangle(0.0f, {-0.8f, -0.8f}, {-0.4f, -0.8f}, {-0.8f, -0.4f}),
      triangle(0.01f, {0.4f, 0.4f}, {0.8f, 0.4f}, {0.4f, 0.8f})};
  auto selected =
      policy.select(initial, overheadProjection(), {0.0f, 2.0f, 0.0f});
  auto descriptor = selected.front();
  std::vector stillVisible{initial.back()};
  selected = policy.select(
      stillVisible, overheadProjection(), {0.0f, 2.0f, 0.0f});
  require(near(selected.front().elevation, descriptor.elevation) &&
              near(selected.front().minimumElevation,
                   descriptor.minimumElevation) &&
              near(selected.front().maximumElevation,
                   descriptor.maximumElevation),
          "a surviving plane changed descriptor and would churn its graph");
}

void appliesViewerSideHysteresisInBothDirections() {
  bw::app::LiquidReflectionSelectionPolicy policy;
  std::vector surfaces{
      triangle(0.0f, {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f})};
  auto sideAt = [&](float cameraElevation) {
    return policy
        .select(surfaces, overheadProjection(),
                {0.0f, cameraElevation, 0.0f})
        .front()
        .viewerAbove;
  };
  require(sideAt(0.0f), "first observation exactly on a plane was not above");
  require(sideAt(-0.049f),
          "the viewer side flipped before crossing 0.05 units downward");
  require(!sideAt(-0.05f),
          "the viewer side did not flip after crossing 0.05 units downward");
  require(!sideAt(0.049f),
          "the viewer side flipped before crossing 0.05 units upward");
  require(sideAt(0.05f),
          "the viewer side did not flip after crossing 0.05 units upward");
}

void resetClearsSelectionAndSideHistory() {
  bw::app::LiquidReflectionSelectionPolicy policy;
  std::vector surfaces{
      triangle(0.0f, {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f})};
  auto selected =
      policy.select(surfaces, overheadProjection(), {0.0f, -0.1f, 0.0f});
  require(!selected.front().viewerAbove,
          "below-plane initialization did not classify the viewer below");
  selected = policy.select(
      surfaces, overheadProjection(), {0.0f, 0.01f, 0.0f});
  require(!selected.front().viewerAbove,
          "side history was not retained inside the crossing deadband");
  policy.reset();
  selected =
      policy.select(surfaces, overheadProjection(), {0.0f, 0.0f, 0.0f});
  require(selected.front().viewerAbove,
          "reset retained stale Planar selection or viewer-side history");
}
}  // namespace

int main() {
  try {
    rejectsTrianglesOutsideTheFrustum();
    groupsTheInclusiveToleranceBoundary();
    ranksByCoverageThenDistance();
    breaksExactTiesByLowestElevation();
    capsSelectionAtFourRankedCandidates();
    retainsSlotsUntilAChallengerClearsTheThreshold();
    reusesTheDepartedPlanesSlot();
    retainsAStableDescriptorForAVisiblePlane();
    appliesViewerSideHysteresisInBothDirections();
    resetClearsSelectionAndSideHistory();
    std::cout << "Liquid reflection selection tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
