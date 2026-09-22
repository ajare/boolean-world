#include "PortalLight.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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

glm::mat4 rendererMatrix(
    bw::core::PortalRigidTransform const& transform) {
  auto x = transform.transformVector({1.0f, 0.0f});
  auto y = transform.transformVector({0.0f, 1.0f});
  auto origin = transform.transformPoint({0.0f, 0.0f});
  glm::mat4 result{1.0f};
  result[0] = {x.x, 0.0f, -x.y, 0.0f};
  result[1] = {0.0f, 1.0f, 0.0f, 0.0f};
  result[2] = {-y.x, 0.0f, y.y, 0.0f};
  result[3] = {
      origin.x, transform.transformElevation(0.0f), -origin.y, 1.0f};
  return result;
}

constexpr std::array<char const*, 12> PortalUniformNames{
    "PORTAL_LIGHT_COUNT",
    "PORTAL_LIGHT_POSITION",
    "PORTAL_LIGHT_SOURCE_POSITION",
    "PORTAL_LIGHT_RADIANCE",
    "PORTAL_LIGHT_APERTURE_CENTRE",
    "PORTAL_LIGHT_APERTURE_TANGENT",
    "PORTAL_LIGHT_APERTURE_FRONT",
    "PORTAL_LIGHT_SOURCE_APERTURE_FRONT",
    "PORTAL_LIGHT_APERTURE_BOUNDS",
    "PORTAL_LIGHT_DESTINATION_TO_SOURCE",
    "PORTAL_LIGHT_SHADOW_PARAMS",
    "PORTAL_LIGHT_SHADOW_BIAS"};
constexpr std::array<char const*, 2> PortalSamplerNames{
    "PASS_POINT_SHADOW_MAP_0", "PASS_POINT_SHADOW_MAP_1"};
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
  result.sourcePosition = playerTorchPosition;
  result.apertureCentre =
      rendererPosition(destination.aperture.centre, 0.0f);
  result.apertureTangent = rendererVector(destination.aperture.tangent);
  result.apertureFront = rendererVector(destination.aperture.front);
  result.sourceApertureCentre =
      rendererPosition(source.aperture.centre, 0.0f);
  result.sourceApertureTangent = rendererVector(source.aperture.tangent);
  result.sourceApertureFront = rendererVector(source.aperture.front);
  result.destinationToSource = glm::inverse(rendererMatrix(transform));
  result.apertureHalfWidth = destination.aperture.width * 0.5f;
  result.apertureBottom = destination.aperture.bottom;
  result.apertureTop = destination.aperture.top;
  result.sourceApertureBottom = source.aperture.bottom;
  result.sourceApertureTop = source.aperture.top;
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
    mpp::ScenePassOverrides& pass,
    std::span<PortalLightShadowAttachment const> lights) {
  if (lights.empty()) return true;
  if (lights.size() > PortalLightAttachmentLimit) return false;

  auto const& shadow = lights.front();
  if (!shadow.sourceShadowMap || !shadow.destinationShadowMap ||
      !std::isfinite(shadow.shadowRange) || shadow.shadowRange <= 0.0f ||
      !std::isfinite(shadow.mapTexelSize) || shadow.mapTexelSize <= 0.0f ||
      !std::isfinite(shadow.constantBias) || shadow.constantBias < 0.0f ||
      !std::isfinite(shadow.normalBias) || shadow.normalBias < 0.0f ||
      !std::isfinite(shadow.filterRadiusTexels) ||
      shadow.filterRadiusTexels < 0.0f ||
      !std::isfinite(shadow.fadeStartNormalized) ||
      shadow.fadeStartNormalized < 0.0f ||
      shadow.fadeStartNormalized > 1.0f) {
    return false;
  }

  auto const& existing = pass.uniforms.getUniformData();
  if (std::ranges::any_of(PortalUniformNames, [&](char const* name) {
        return existing.contains(name);
      }) ||
      std::ranges::any_of(PortalSamplerNames, [&](char const* name) {
        return pass.samplers.contains(name);
      })) {
    return false;
  }

  auto const& light = shadow.light;
  auto overrides = pass.uniforms;
  auto samplers = pass.samplers;
  overrides.setUniform("PORTAL_LIGHT_COUNT", int32_t{1});
  overrides.setUniform("PORTAL_LIGHT_POSITION", light.position);
  overrides.setUniform("PORTAL_LIGHT_SOURCE_POSITION", light.sourcePosition);
  overrides.setUniform("PORTAL_LIGHT_RADIANCE", light.radiance);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_CENTRE", light.apertureCentre);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_TANGENT", light.apertureTangent);
  overrides.setUniform("PORTAL_LIGHT_APERTURE_FRONT", light.apertureFront);
  overrides.setUniform(
      "PORTAL_LIGHT_SOURCE_APERTURE_FRONT", light.sourceApertureFront);
  overrides.setUniform(
      "PORTAL_LIGHT_APERTURE_BOUNDS",
      glm::vec3{light.apertureHalfWidth, light.apertureBottom,
                light.apertureTop});
  overrides.setUniform(
      "PORTAL_LIGHT_DESTINATION_TO_SOURCE", light.destinationToSource);
  overrides.setUniform(
      "PORTAL_LIGHT_SHADOW_PARAMS",
      glm::vec4{shadow.mapTexelSize, shadow.filterRadiusTexels,
                shadow.pcf ? 1.0f : 0.0f, shadow.shadowRange});
  overrides.setUniform(
      "PORTAL_LIGHT_SHADOW_BIAS",
      glm::vec3{shadow.constantBias, shadow.normalBias,
                shadow.fadeStartNormalized});
  samplers.emplace("PASS_POINT_SHADOW_MAP_0", shadow.sourceShadowMap);
  samplers.emplace(
      "PASS_POINT_SHADOW_MAP_1", shadow.destinationShadowMap);
  pass.uniforms = std::move(overrides);
  pass.samplers = std::move(samplers);
  return true;
}

bool AttachPortalLightsToPass(
    mpp::AuxiliarySceneView& view,
    std::span<PortalLightShadowAttachment const> lights) {
  mpp::ScenePassOverrides pass;
  pass.uniforms = view.uniformOverrides;
  pass.samplers = view.samplerOverrides;
  if (!AttachPortalLightsToPass(pass, lights)) return false;
  view.uniformOverrides = std::move(pass.uniforms);
  view.samplerOverrides = std::move(pass.samplers);
  return true;
}
