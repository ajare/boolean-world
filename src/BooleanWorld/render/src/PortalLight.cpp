#include "PortalLight.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>

namespace {
constexpr float PlaneTolerance = 0.0001f;

wp::Vector2 worldPlanePosition(glm::vec3 const& rendererPosition) {
  return {rendererPosition.x, -rendererPosition.z};
}

glm::vec3 rendererPosition(
    wp::Vector2 const& worldPlanePosition, float elevation) {
  return {worldPlanePosition.x, elevation, -worldPlanePosition.y};
}

glm::vec3 rendererVector(wp::Vector2 const& worldPlaneVector) {
  return {worldPlaneVector.x, 0.0f, -worldPlaneVector.y};
}

constexpr std::array<char const*, 7> PortalUniformNames{
    "PORTAL_LIGHT_COUNT",
    "PORTAL_LIGHT_POSITION",
    "PORTAL_LIGHT_RADIANCE",
    "PORTAL_LIGHT_APERTURE_CENTRE",
    "PORTAL_LIGHT_APERTURE_TANGENT",
    "PORTAL_LIGHT_APERTURE_FRONT",
    "PORTAL_LIGHT_APERTURE_BOUNDS"};
}  // namespace

std::optional<PortalLightAttachment> BuildPortalLightAttachment(
    bw::core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpoint,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch) {
  if (!pair.active || sourceEndpoint >= pair.endpoints.size() ||
      !std::isfinite(playerTorch.attenuationRadius) ||
      !std::isfinite(playerTorch.attenuationFalloff) ||
      playerTorch.attenuationRadius <= 0.0f ||
      playerTorch.attenuationFalloff < 0.0f ||
      playerTorch.attenuationFalloff > playerTorch.attenuationRadius) {
    return std::nullopt;
  }

  auto const& source = pair.endpoints[sourceEndpoint];
  auto const destinationEndpoint = 1u - sourceEndpoint;
  auto const& destination = pair.endpoints[destinationEndpoint];
  if (!source.resolved || !destination.resolved) return std::nullopt;

  auto const lightPlane = worldPlanePosition(playerTorchPosition);
  if (source.aperture.front.dot(lightPlane - source.aperture.centre) <=
      PlaneTolerance) {
    return std::nullopt;
  }

  auto const transform =
      bw::core::BuildPortalRigidTransform(pair, sourceEndpoint);
  auto const virtualPlane = transform.transformPoint(lightPlane);

  PortalLightAttachment result;
  result.position = rendererPosition(
      virtualPlane,
      transform.transformElevation(playerTorchPosition.y));
  result.apertureCentre =
      rendererPosition(destination.aperture.centre, 0.0f);
  result.apertureTangent = rendererVector(destination.aperture.tangent);
  result.apertureFront = rendererVector(destination.aperture.front);
  result.apertureHalfWidth = destination.aperture.width * 0.5f;
  result.apertureBottom = destination.aperture.bottom;
  result.apertureTop = destination.aperture.top;
  result.attenuationRadius = playerTorch.attenuationRadius;
  result.attenuationFalloff = playerTorch.attenuationFalloff;
  return result;
}

bool PortalLightAdmitsReceiver(
    PortalLightAttachment const& light,
    glm::vec3 const& receiverPosition) {
  auto const front = glm::normalize(light.apertureFront);
  auto const receiverSide =
      glm::dot(receiverPosition - light.apertureCentre, front);
  auto const lightSide =
      glm::dot(light.position - light.apertureCentre, front);
  if (receiverSide <= PlaneTolerance || lightSide >= -PlaneTolerance) {
    return false;
  }

  auto const denominator = lightSide - receiverSide;
  if (std::abs(denominator) <= PlaneTolerance) return false;
  auto const t = -receiverSide / denominator;
  if (t < 0.0f || t > 1.0f) return false;

  auto const intersection =
      receiverPosition + t * (light.position - receiverPosition);
  auto const tangentDistance = std::abs(glm::dot(
      intersection - light.apertureCentre,
      glm::normalize(light.apertureTangent)));
  return tangentDistance <= light.apertureHalfWidth + PlaneTolerance &&
         intersection.y >= light.apertureBottom - PlaneTolerance &&
         intersection.y <= light.apertureTop + PlaneTolerance;
}

bool AttachPortalLightsToPass(
    mpp::AuxiliarySceneView& view,
    std::span<PortalLightAttachment const> lights) {
  if (lights.empty()) return true;
  if (lights.size() > PortalLightAttachmentLimit) return false;

  auto const& existing = view.uniformOverrides.getUniformData();
  if (std::ranges::any_of(PortalUniformNames, [&](char const* name) {
        return existing.contains(name);
      })) {
    return false;
  }

  auto const& light = lights.front();
  auto overrides = view.uniformOverrides;
  overrides.setUniform("PORTAL_LIGHT_COUNT", int32_t{1});
  overrides.setUniform("PORTAL_LIGHT_POSITION", light.position);
  overrides.setUniform("PORTAL_LIGHT_RADIANCE", light.radiance);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_CENTRE", light.apertureCentre);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_TANGENT", light.apertureTangent);
  overrides.setUniform("PORTAL_LIGHT_APERTURE_FRONT", light.apertureFront);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_BOUNDS",
      glm::vec3{light.apertureHalfWidth, light.apertureBottom,
                light.apertureTop});
  view.uniformOverrides = std::move(overrides);
  return true;
}
