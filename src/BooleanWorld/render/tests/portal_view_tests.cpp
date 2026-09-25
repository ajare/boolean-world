#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <PortalView.h>

namespace {
void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 1e-4f) {
  return std::abs(left - right) <= tolerance;
}

bw::core::ResolvedPortalPair pair(
    uint32_t layerId, uint32_t pairId,
    wp::Vector2 sourceCentre, wp::Vector2 sourceFront,
    float width = 2.0f, float bottom = -1.0f, float top = 1.0f) {
  bw::core::ResolvedPortalPair result;
  result.layerId = layerId;
  result.pairId = pairId;
  result.active = true;
  result.endpoints[0].endpointId = 0;
  result.endpoints[0].resolved = true;
  result.endpoints[0].aperture = {
      sourceCentre, {1.0f, 0.0f}, sourceFront, width, bottom, top, {0}};
  result.endpoints[1].endpointId = 1;
  result.endpoints[1].resolved = true;
  result.endpoints[1].aperture = {
      {10.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, width, bottom + 3.0f, top + 3.0f, {1}};
  return result;
}

bw::core::ResolvedPortalPair translatedLoopPair(
    uint32_t layerId, uint32_t pairId, float centreX = 0.0f) {
  auto result = pair(
      layerId, pairId, {centreX, 4.0f}, {0.0f, -1.0f}, 1.5f);
  // Separate planes: the source remains visible beyond the destination clip
  // plane. Coincident endpoints merely test whether the exit surface leaks
  // through clipping, not a physically visible recursive Portal loop.
  result.endpoints[1].aperture = {
      {centreX, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, 1.5f, -1.0f, 1.0f, {1}};
  return result;
}

void selectionRejectsInvisibleEndpointsAndUsesDeterministicOrdering() {
  auto projection = glm::perspective(
      glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
  auto view = glm::lookAt(
      glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
      glm::vec3{0.0f, 1.0f, 0.0f});

  auto backFacing = pair(0, 1, {0.0f, 4.0f}, {0.0f, 1.0f});
  auto outside = pair(0, 2, {100.0f, 4.0f}, {0.0f, -1.0f});
  auto small = pair(0, 3, {0.0f, 5.0f}, {0.0f, -1.0f}, 1.0f);
  auto large = pair(0, 4, {0.0f, 5.0f}, {0.0f, -1.0f}, 2.0f);
  std::vector candidates{backFacing, outside, small, large};
  auto selected = SelectPortalView(
      candidates, projection * view, {0.0f, 0.0f, 0.0f});
  require(selected && selected->key.pairId == 4,
          "Portal selection did not reject invisible endpoints or rank projected coverage");

  auto nearPair = pair(2, 9, {0.0f, 5.0f}, {0.0f, -1.0f});
  auto farPair = pair(1, 8, {1.0f, 5.0f}, {0.0f, -1.0f});
  candidates = {farPair, nearPair};
  selected = SelectPortalView(candidates, projection * view, {0.0f, 0.0f, 0.0f});
  require(selected && selected->key.pairId == 9,
          "Portal distance did not break equal-shape selection deterministically");

  auto highIdentity = pair(2, 4, {0.0f, 5.0f}, {0.0f, -1.0f});
  auto lowIdentity = pair(1, 7, {0.0f, 5.0f}, {0.0f, -1.0f});
  candidates = {highIdentity, lowIdentity};
  selected = SelectPortalView(candidates, projection * view, {0.0f, 0.0f, 0.0f});
  require(selected && selected->key.layerId == 1,
          "stable Portal identity did not break exact coverage/distance ties");
}

void crossingPlaneRetainsThePortalWithoutAdmittingItsBackSide() {
  auto projection = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
  std::vector pairs{pair(0, 0, {0.0f, 0.0f}, {0.0f, -1.0f})};
  for (float distance : {0.2f, 0.05f, 0.001f, 0.0f}) {
    glm::vec3 eye{0.0f, 0.0f, distance};
    auto view = glm::lookAt(eye, eye + glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0});
    auto selected = SelectPortalView(pairs, projection * view, eye);
    require(selected && selected->key.endpointId == 0 && selected->projectedCoverage > 3.9f,
            "near-plane clipping removed the crossing aperture");
  }
  for (glm::vec3 eye : {glm::vec3{0, 0, -0.01f}, glm::vec3{2, 0, 0}}) {
    auto view = glm::lookAt(eye, eye + glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0});
    require(!SelectPortalView(pairs, projection * view, eye),
            "crossing tolerance admitted a back-side or outside-aperture camera");
  }
  auto away = glm::lookAt(glm::vec3{0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 0});
  require(!SelectPortalView(pairs, projection * away, glm::vec3{0}),
          "coplanar camera looking away selected the aperture");
}

void plannerSelectsSeveralEndpointsAndSharesOnlyEquivalentWork() {
  auto projection = glm::perspective(
      glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
  auto view = glm::lookAt(
      glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
      glm::vec3{0.0f, 1.0f, 0.0f});
  std::vector pairs{
      translatedLoopPair(0, 10, -0.75f),
      translatedLoopPair(0, 11, 0.75f)};
  PortalViewLimits limits;
  limits.maxRecursionDepth = 1;
  PortalViewPlanner planner(limits);
  auto plan = planner.build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240);

  require(plan.rootChildren.size() == 2,
          "several visible Portal endpoints did not receive live views");
  require(plan.renderedPassCount == 1 &&
              plan.rootChildren[0].childNode == plan.rootChildren[1].childNode,
          "exact equivalent transformed camera states did not safely share work");
  require(plan.deepestFirst.size() == 1,
          "equivalent Portal work produced duplicate auxiliary passes");
}

void loopsTerminateOnlyAtNamedLimitsAndKeepStableSlots() {
  auto projection = glm::perspective(
      glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
  auto view = glm::lookAt(
      glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
      glm::vec3{0.0f, 1.0f, 0.0f});
  std::vector pairs{translatedLoopPair(0, 20)};

  PortalViewLimits limits;
  limits.maxRecursionDepth = 3;
  PortalViewPlanner planner(limits);
  auto first = planner.build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240);
  require(first.renderedPassCount == 3 && first.deepestFirst.size() == 3,
          "a mutually visible Portal loop did not produce a bounded deterministic pass count");
  require(first.cutoffCount(PortalViewCutoffReason::RecursionDepth) != 0,
          "Portal loop did not report its recursion-depth cutoff");
  auto revisits = std::ranges::count_if(
      first.diagnostics, [](auto const& diagnostic) {
        return diagnostic.selected && diagnostic.endpoint.pairId == 20;
      });
  require(revisits == 3,
          "revisiting a Portal endpoint incorrectly terminated visual recursion");

  auto movedView = glm::lookAt(
      glm::vec3{0.001f, 0.0f, 0.0f},
      glm::vec3{0.001f, 0.0f, -1.0f},
      glm::vec3{0.0f, 1.0f, 0.0f});
  pairs[0].endpoints[0].endpointId = 17;
  pairs[0].endpoints[1].endpointId = 93;
  pairs[0].traversalOrder = {93, 17};
  std::swap(pairs[0].endpoints[0], pairs[0].endpoints[1]);
  auto stable = planner.build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240);
  require(stable.renderedPassCount == first.renderedPassCount,
          "stable endpoint identities changed recursive output");
  for (size_t index = 0; index < first.nodes.size(); ++index) {
    require(stable.nodes[index].auxiliary.view ==
                first.nodes[index].auxiliary.view,
            "endpoint storage order changed recursive cameras");
  }
  require(stable.rootChildren.front().endpoint.endpointId == 17,
          "recursive view keys used endpoint slots instead of identity");
  first = stable;
  std::swap(pairs[0].endpoints[0], pairs[0].endpoints[1]);
  auto second = planner.build(
      pairs, movedView, projection, 0.1f, 100.0f, 320, 240);
  require(second.renderedPassCount == first.renderedPassCount &&
              second.nodes.size() == first.nodes.size(),
          "an unchanged Portal loop changed its pass budget");
  for (size_t index = 0; index < first.nodes.size(); ++index) {
    require(first.nodes[index].slot == second.nodes[index].slot,
            "stable Portal slots churned without a visibility change");
  }

  limits.maxRecursionDepth = 10;
  limits.maxTargets = 2;
  limits.maxRenderedPasses = 8;
  auto targetLimited = PortalViewPlanner(limits).build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240);
  require(targetLimited.renderedPassCount == 2 &&
              targetLimited.cutoffCount(
                  PortalViewCutoffReason::TargetBudget) != 0,
          "Portal target budget did not terminate and diagnose a loop");

  limits.maxTargets = 8;
  limits.maxRenderedPasses = 2;
  auto frameLimited = PortalViewPlanner(limits).build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240);
  require(frameLimited.renderedPassCount == 2 &&
              frameLimited.cutoffCount(
                  PortalViewCutoffReason::FrameBudget) != 0,
          "Portal frame budget did not terminate and diagnose a loop");
}

void invisibleAndSubThresholdBranchesConsumeNoSlots() {
  auto projection = glm::perspective(
      glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f);
  auto view = glm::lookAt(
      glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
      glm::vec3{0.0f, 1.0f, 0.0f});
  auto visible = pair(0, 30, {0.0f, 5.0f}, {0.0f, -1.0f});
  auto backFacing = pair(0, 31, {0.0f, 4.0f}, {0.0f, 1.0f});
  auto outside = pair(0, 32, {100.0f, 4.0f}, {0.0f, -1.0f});
  auto tiny = pair(
      0, 33, {0.0f, 80.0f}, {0.0f, -1.0f}, 0.01f,
      -0.005f, 0.005f);
  std::vector pairs{visible, backFacing, outside, tiny};
  PortalViewLimits limits;
  limits.maxRecursionDepth = 1;
  limits.minimumProjectedCoverage = 0.001f;
  PortalViewPlanner planner(limits);
  auto plan = planner.build(
      pairs, view, projection, 0.1f, 100.0f, 320, 240,
      [](PortalEndpointKey const& key,
         bw::core::ResolvedAperture const&, glm::mat4 const&) {
        return key.pairId == 30;
      });

  require(plan.renderedPassCount == 0 && plan.rootChildren.empty(),
          "culled Portal branches consumed recursive slots");
  require(plan.cutoffCount(
              PortalViewCutoffReason::VisibilityBackFacing) != 0 &&
              plan.cutoffCount(
                  PortalViewCutoffReason::VisibilityOutOfFrustum) != 0 &&
              plan.cutoffCount(
                  PortalViewCutoffReason::VisibilityOccluded) != 0 &&
              plan.cutoffCount(PortalViewCutoffReason::ProjectedArea) != 0,
          "Portal visibility and projected-area cutoff diagnostics were incomplete");
}

void observingCameraUsesTheCanonicalRigidTransformAndExactProjection() {
  auto portalPair = pair(3, 5, {0.0f, 4.0f}, {0.0f, -1.0f});
  portalPair.endpoints[0].endpointId = 17;
  portalPair.endpoints[1].endpointId = 93;
  portalPair.traversalOrder = {93, 17};
  std::swap(portalPair.endpoints[0], portalPair.endpoints[1]);
  SelectedPortalView selected{{3, 5, 17}, &portalPair, 1.0f, 4.0f};
  auto eye = glm::vec3{0.75f, 0.25f, 0.0f};
  auto view = glm::lookAt(
      eye, eye + glm::normalize(glm::vec3{0.2f, 0.1f, -1.0f}),
      glm::vec3{0.0f, 1.0f, 0.0f});
  auto projection = glm::perspective(
      glm::radians(67.0f), 13.0f / 7.0f, 0.2f, 300.0f);
  auto built = BuildPortalView(
      selected, view, projection, 0.2f, 300.0f, 130, 70);

  auto canonical = bw::core::BuildPortalRigidTransform(portalPair, 17);
  auto expectedPlane = canonical.transformPoint({eye.x, -eye.z});
  auto transformedEye = built.sourceToDestination * glm::vec4(eye, 1.0f);
  require(
      near(transformedEye.x, expectedPlane.x) &&
          near(transformedEye.y, canonical.transformElevation(eye.y)) &&
          near(transformedEye.z, -expectedPlane.y),
      "rendering did not transform the observing camera through the canonical Portal transform");
  require(built.auxiliary.projection == projection &&
              built.auxiliary.width == 130 && built.auxiliary.height == 70,
          "Portal rendering changed camera projection/aspect or target dimensions");

  auto sourcePoint = glm::vec4{0.25f, 0.4f, -4.0f, 1.0f};
  auto direct = projection * view * sourcePoint;
  auto projective = built.sourceProjectiveTransform * sourcePoint;
  require(near(direct.x / direct.w, projective.x / projective.w) &&
              near(direct.y / direct.w, projective.y / projective.w),
          "Portal aperture sampling is not projectively aligned with the observing camera");

  auto destination = bw::core::NextPortalEndpoint(portalPair, 17)->aperture;
  auto destinationCentre = glm::vec3{
      destination.centre.x, destination.bottom, -destination.centre.y};
  auto destinationFront = glm::vec3{
      destination.front.x, 0.0f, -destination.front.y};
  require(near(
              glm::dot(destinationFront, destinationCentre) +
                  built.auxiliary.worldClipPlane.w,
              -mpp::AuxiliaryViewClipSeamBias) &&
              glm::dot(
                  glm::vec3(built.auxiliary.worldClipPlane),
                  destinationFront) > 0.99f,
          "Portal oblique clip plane does not retain the destination front half-space");

  auto clipped = mpp::buildObliquelyClippedVirtualCamera(
      built.auxiliary.view, built.auxiliary.projection,
      built.auxiliary.worldClipPlane, built.auxiliary.seamBias);
  auto aperturePoint = destinationCentre;
  aperturePoint.y = (destination.bottom + destination.top) * 0.5f;
  auto onPlane = clipped.projection * clipped.view * glm::vec4(aperturePoint, 1.0f);
  auto inside = clipped.projection * clipped.view *
                glm::vec4(aperturePoint + destinationFront, 1.0f);
  require(onPlane.z + onPlane.w < 0.0f && inside.z + inside.w > 0.0f,
          "destination aperture back face occludes the Portal's virtual view");
}
}  // namespace

int main() {
  try {
    selectionRejectsInvisibleEndpointsAndUsesDeterministicOrdering();
    crossingPlaneRetainsThePortalWithoutAdmittingItsBackSide();
    plannerSelectsSeveralEndpointsAndSharesOnlyEquivalentWork();
    loopsTerminateOnlyAtNamedLimitsAndKeepStableSlots();
    invisibleAndSubThresholdBranchesConsumeNoSlots();
    observingCameraUsesTheCanonicalRigidTransformAndExactProjection();
    std::cout << "Bounded Portal view planning and transforms passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
