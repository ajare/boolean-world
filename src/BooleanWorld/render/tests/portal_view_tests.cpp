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

void observingCameraUsesTheCanonicalRigidTransformAndExactProjection() {
  auto portalPair = pair(3, 5, {0.0f, 4.0f}, {0.0f, -1.0f});
  SelectedPortalView selected{{3, 5, 0}, &portalPair, 0, 1.0f, 4.0f};
  auto eye = glm::vec3{0.75f, 0.25f, 0.0f};
  auto view = glm::lookAt(
      eye, eye + glm::normalize(glm::vec3{0.2f, 0.1f, -1.0f}),
      glm::vec3{0.0f, 1.0f, 0.0f});
  auto projection = glm::perspective(
      glm::radians(67.0f), 13.0f / 7.0f, 0.2f, 300.0f);
  auto built = BuildPortalView(
      selected, view, projection, 0.2f, 300.0f, 130, 70);

  auto canonical = bw::core::BuildPortalRigidTransform(portalPair, 0);
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

  auto destination = portalPair.endpoints[1].aperture;
  auto destinationCentre = glm::vec3{
      destination.centre.x, destination.bottom, -destination.centre.y};
  auto destinationFront = glm::vec3{
      destination.front.x, 0.0f, -destination.front.y};
  require(near(
              glm::dot(destinationFront, destinationCentre) +
                  built.auxiliary.worldClipPlane.w,
              0.0f) &&
              glm::dot(
                  glm::vec3(built.auxiliary.worldClipPlane),
                  destinationFront) > 0.99f,
          "Portal oblique clip plane does not retain the destination front half-space");
}
}  // namespace

int main() {
  try {
    selectionRejectsInvisibleEndpointsAndUsesDeterministicOrdering();
    observingCameraUsesTheCanonicalRigidTransformAndExactProjection();
    std::cout << "Portal view selection and transforms passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
