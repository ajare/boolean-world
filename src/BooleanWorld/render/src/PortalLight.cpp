#include "PortalLight.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <ranges>
#include <string>
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

bool validOptions(bw::app::PlayerTorchOptions const& playerTorch) {
  return std::isfinite(playerTorch.attenuationRadius) &&
         std::isfinite(playerTorch.attenuationFalloff) &&
         playerTorch.attenuationRadius > 0.0f &&
         playerTorch.attenuationFalloff >= 0.0f &&
         playerTorch.attenuationFalloff <= playerTorch.attenuationRadius;
}

float attenuation(
    float distance, bw::app::PlayerTorchOptions const& playerTorch) {
  if (distance >= playerTorch.attenuationRadius) return 0.0f;
  auto edge = 1.0f;
  if (playerTorch.attenuationFalloff > 0.0f) {
    auto start = playerTorch.attenuationRadius -
                 playerTorch.attenuationFalloff;
    auto t = std::clamp(
        (distance - start) / playerTorch.attenuationFalloff, 0.0f, 1.0f);
    auto smooth = t * t * (3.0f - 2.0f * t);
    edge = 1.0f - smooth;
  }
  return edge /
         (1.0f + distance * 0.04f + distance * distance * 0.0015f);
}

PortalLightEndpointKey endpointKey(
    bw::core::ResolvedPortalPair const& pair, uint32_t endpointId) {
  return {pair.layerId, pair.pairId, endpointId};
}

std::optional<PortalLightPathHop> buildHop(
    bw::core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpointId,
    glm::vec3 const& inputLightPosition) {
  if (!pair.active) return std::nullopt;
  auto const* sourceEndpoint =
      bw::core::FindPortalEndpoint(pair, sourceEndpointId);
  auto const* destinationEndpoint =
      bw::core::NextPortalEndpoint(pair, sourceEndpointId);
  if (!sourceEndpoint || !destinationEndpoint) return std::nullopt;
  auto const& source = *sourceEndpoint;
  auto const& destination = *destinationEndpoint;
  if (!source.resolved || !destination.resolved) return std::nullopt;

  auto const lightPlane = worldPlanePosition(inputLightPosition);
  if (source.aperture.front.dot(lightPlane - source.aperture.centre) <=
      PlaneTolerance) {
    return std::nullopt;
  }

  auto const transform = bw::core::BuildPortalRigidTransform(
      pair, source.endpointId);
  auto const virtualPlane = transform.transformPoint(lightPlane);

  PortalLightPathHop hop;
  hop.sourceEndpoint = endpointKey(pair, source.endpointId);
  hop.destinationEndpoint = endpointKey(pair, destination.endpointId);
  hop.virtualLightPosition = rendererPosition(
      virtualPlane, transform.transformElevation(inputLightPosition.y));
  hop.sourceApertureCentre =
      rendererPosition(source.aperture.centre, 0.0f);
  hop.sourceApertureTangent = rendererVector(source.aperture.tangent);
  hop.sourceApertureFront = rendererVector(source.aperture.front);
  hop.destinationApertureCentre =
      rendererPosition(destination.aperture.centre, 0.0f);
  hop.destinationApertureTangent =
      rendererVector(destination.aperture.tangent);
  hop.destinationApertureFront = rendererVector(destination.aperture.front);
  hop.destinationToSource = glm::inverse(rendererMatrix(transform));
  hop.apertureHalfWidth = destination.aperture.width * 0.5f;
  hop.sourceApertureBottom = source.aperture.bottom;
  hop.sourceApertureTop = source.aperture.top;
  hop.destinationApertureBottom = destination.aperture.bottom;
  hop.destinationApertureTop = destination.aperture.top;
  return hop;
}

bool gate(
    PortalLightPathHop const& hop,
    glm::vec3 const& receiverPosition,
    glm::vec3 const& lightPosition,
    glm::vec3& intersection) {
  auto const front = glm::normalize(hop.destinationApertureFront);
  auto const receiverSide = glm::dot(
      receiverPosition - hop.destinationApertureCentre, front);
  auto const lightSide = glm::dot(
      lightPosition - hop.destinationApertureCentre, front);
  if (receiverSide <= PlaneTolerance || lightSide >= -PlaneTolerance) {
    return false;
  }

  auto const denominator = lightSide - receiverSide;
  if (std::abs(denominator) <= PlaneTolerance) return false;
  auto const t = -receiverSide / denominator;
  if (t < 0.0f || t > 1.0f) return false;

  intersection = receiverPosition + t * (lightPosition - receiverPosition);
  auto const tangentDistance = std::abs(glm::dot(
      intersection - hop.destinationApertureCentre,
      glm::normalize(hop.destinationApertureTangent)));
  return tangentDistance <= hop.apertureHalfWidth + PlaneTolerance &&
         intersection.y >=
             hop.destinationApertureBottom - PlaneTolerance &&
         intersection.y <= hop.destinationApertureTop + PlaneTolerance;
}

bool apertureVisibleThroughPath(
    PortalLightAttachment const& light,
    bw::core::ResolvedAperture const& aperture) {
  auto centreElevation = (aperture.bottom + aperture.top) * 0.5f;
  auto tangent = rendererVector(aperture.tangent);
  auto centre = rendererPosition(aperture.centre, centreElevation);
  auto halfWidth = aperture.width * 0.49f;
  auto halfHeight = (aperture.top - aperture.bottom) * 0.49f;
  constexpr std::array<float, 3> offsets{-1.0f, 0.0f, 1.0f};
  return std::ranges::any_of(offsets, [&](float across) {
    return std::ranges::any_of(offsets, [&](float vertical) {
      return PortalLightAdmitsReceiver(
          light, centre + tangent * (across * halfWidth) +
                     glm::vec3{0.0f, vertical * halfHeight, 0.0f});
    });
  });
}

bool pathContainsEndpoint(
    PortalLightAttachment const& light,
    PortalLightEndpointKey const& endpoint) {
  return std::ranges::any_of(light.hops, [&](auto const& hop) {
    return hop.sourceEndpoint == endpoint ||
           hop.destinationEndpoint == endpoint;
  });
}

bool near(float left, float right, float tolerance) {
  return std::abs(left - right) <= tolerance;
}

bool near(glm::vec3 const& left, glm::vec3 const& right, float tolerance) {
  return near(left.x, right.x, tolerance) &&
         near(left.y, right.y, tolerance) &&
         near(left.z, right.z, tolerance);
}

bool near(glm::mat4 const& left, glm::mat4 const& right, float tolerance) {
  for (uint32_t column = 0; column < 4; ++column) {
    for (uint32_t row = 0; row < 4; ++row) {
      if (!near(left[column][row], right[column][row], tolerance)) {
        return false;
      }
    }
  }
  return true;
}

bool equivalent(
    PortalLightAttachment const& left,
    PortalLightAttachment const& right) {
  if (left.hops.size() != right.hops.size() ||
      !near(left.position, right.position, PortalLightPositionTolerance)) {
    return false;
  }
  for (size_t index = 0; index < left.hops.size(); ++index) {
    auto const& a = left.hops[index];
    auto const& b = right.hops[index];
    if (!near(
            a.sourceApertureCentre, b.sourceApertureCentre,
            PortalLightPositionTolerance) ||
        !near(
            a.destinationApertureCentre, b.destinationApertureCentre,
            PortalLightPositionTolerance) ||
        !near(
            a.sourceApertureTangent, b.sourceApertureTangent,
            PortalLightDirectionTolerance) ||
        !near(
            a.sourceApertureFront, b.sourceApertureFront,
            PortalLightDirectionTolerance) ||
        !near(
            a.destinationApertureTangent,
            b.destinationApertureTangent,
            PortalLightDirectionTolerance) ||
        !near(
            a.destinationApertureFront, b.destinationApertureFront,
            PortalLightDirectionTolerance) ||
        !near(
            a.destinationToSource, b.destinationToSource,
            PortalLightTransformTolerance) ||
        !near(
            a.apertureHalfWidth, b.apertureHalfWidth,
            PortalLightPositionTolerance) ||
        !near(
            a.sourceApertureBottom, b.sourceApertureBottom,
            PortalLightPositionTolerance) ||
        !near(
            a.sourceApertureTop, b.sourceApertureTop,
            PortalLightPositionTolerance) ||
        !near(
            a.destinationApertureBottom,
            b.destinationApertureBottom,
            PortalLightPositionTolerance) ||
        !near(
            a.destinationApertureTop, b.destinationApertureTop,
            PortalLightPositionTolerance)) {
      return false;
    }
  }
  return true;
}

std::string lightUniform(char const* field, size_t light) {
  return "PORTAL_LIGHT_" + std::string(field) + "_" +
         std::to_string(light);
}

std::string hopUniform(
    char const* field, size_t light, size_t hop) {
  return lightUniform(field, light) + "_" + std::to_string(hop);
}

std::string shadowSampler(size_t light, size_t leg) {
  return "PASS_POINT_SHADOW_MAP_" +
         std::to_string(light * (PortalLightHopLimit + 1) + leg);
}
}  // namespace

std::string_view PortalLightDiagnosticReasonText(
    PortalLightDiagnosticReason reason) {
  switch (reason) {
    case PortalLightDiagnosticReason::Retained:
      return "retained";
    case PortalLightDiagnosticReason::Inactive:
      return "inactive";
    case PortalLightDiagnosticReason::SourceBackFacing:
      return "source-back-facing";
    case PortalLightDiagnosticReason::ApertureInvisible:
      return "aperture-invisible-through-path";
    case PortalLightDiagnosticReason::EndpointCycle:
      return "endpoint-cycle";
    case PortalLightDiagnosticReason::EquivalentLight:
      return "equivalent-light";
    case PortalLightDiagnosticReason::HopBudget:
      return "hop-budget";
    case PortalLightDiagnosticReason::VirtualLightBudget:
      return "virtual-light-budget";
    case PortalLightDiagnosticReason::ShadowPassBudget:
      return "shadow-pass-budget";
    case PortalLightDiagnosticReason::ShadowUnavailable:
      return "shadow-unavailable";
  }
  return "unknown";
}

uint32_t PortalLightPlan::count(
    PortalLightDiagnosticReason reason) const {
  return static_cast<uint32_t>(std::ranges::count_if(
      diagnostics,
      [&](auto const& diagnostic) { return diagnostic.reason == reason; }));
}

std::optional<PortalLightAttachment> BuildPortalLightAttachment(
    bw::core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpointId,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch) {
  if (!validOptions(playerTorch)) return std::nullopt;
  auto hop = buildHop(pair, sourceEndpointId, playerTorchPosition);
  if (!hop) return std::nullopt;

  auto sourceCentre = glm::vec3{
      hop->sourceApertureCentre.x,
      (hop->sourceApertureBottom + hop->sourceApertureTop) * 0.5f,
      hop->sourceApertureCentre.z};
  auto distance = glm::distance(playerTorchPosition, sourceCentre);
  auto direction = glm::normalize(playerTorchPosition - sourceCentre);
  auto facing = std::max(
      glm::dot(glm::normalize(hop->sourceApertureFront), direction), 0.0f);
  auto area = (hop->apertureHalfWidth * 2.0f) *
              (hop->sourceApertureTop - hop->sourceApertureBottom);

  PortalLightAttachment result;
  result.position = hop->virtualLightPosition;
  result.sourcePosition = playerTorchPosition;
  result.attenuationRadius = playerTorch.attenuationRadius;
  result.attenuationFalloff = playerTorch.attenuationFalloff;
  result.strength = PlayerTorchRadiance * attenuation(distance, playerTorch);
  result.visibility = std::clamp(
      area * facing / std::max(distance * distance, PlaneTolerance),
      0.0f, 1.0f);
  result.path.push_back(hop->sourceEndpoint);
  result.hops.push_back(std::move(*hop));
  return result;
}

PortalLightPlan PlanPortalLights(
    std::span<bw::core::ResolvedPortalPair const> pairs,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch,
    PortalLightLimits limits) {
  PortalLightPlan plan;
  limits.maxHops = std::min(limits.maxHops, PortalLightHopLimit);
  limits.maxVirtualLights =
      std::min(limits.maxVirtualLights, PortalLightAttachmentLimit);
  limits.maxShadowPasses =
      std::min(limits.maxShadowPasses, PortalLightShadowPassLimit);
  if (!validOptions(playerTorch)) return plan;

  struct Endpoint {
    bw::core::ResolvedPortalPair const* pair{};
    PortalLightEndpointKey key{};
  };
  std::vector<Endpoint> endpoints;
  for (auto const& pair : pairs) {
    for (auto const& source : pair.endpoints) {
      endpoints.push_back({&pair, endpointKey(pair, source.endpointId)});
    }
  }
  std::ranges::sort(endpoints, {}, &Endpoint::key);

  std::vector<PortalLightAttachment> candidates;
  auto addDiagnostic = [&](std::vector<PortalLightEndpointKey> path,
                           PortalLightDiagnosticReason reason,
                           float strength = 0.0f,
                           float visibility = 0.0f) {
    plan.diagnostics.push_back(
        {std::move(path), reason, strength, visibility, {}});
  };

  std::function<void(std::optional<PortalLightAttachment> const&)> expand;
  expand = [&](std::optional<PortalLightAttachment> const& parent) {
    auto depth = parent ? static_cast<uint32_t>(parent->hops.size()) : 0u;
    for (auto const& endpoint : endpoints) {
      auto path = parent ? parent->path
                         : std::vector<PortalLightEndpointKey>{};
      path.push_back(endpoint.key);
      auto const& pair = *endpoint.pair;
      auto const* source =
          bw::core::FindPortalEndpoint(pair, endpoint.key.endpointId);
      auto const* destination =
          bw::core::NextPortalEndpoint(pair, endpoint.key.endpointId);

      if (!pair.active || !source || !destination ||
          !source->resolved || !destination->resolved) {
        addDiagnostic(std::move(path), PortalLightDiagnosticReason::Inactive);
        continue;
      }
      if (parent &&
          (pathContainsEndpoint(*parent, endpoint.key) ||
           pathContainsEndpoint(*parent, endpointKey(pair, destination->endpointId)))) {
        addDiagnostic(
            std::move(path), PortalLightDiagnosticReason::EndpointCycle,
            parent->strength, parent->visibility);
        continue;
      }
      if (depth >= limits.maxHops) {
        addDiagnostic(
            std::move(path), PortalLightDiagnosticReason::HopBudget,
            parent ? parent->strength : 0.0f,
            parent ? parent->visibility : 0.0f);
        continue;
      }

      auto inputPosition = parent ? parent->position : playerTorchPosition;
      auto hop = buildHop(pair, endpoint.key.endpointId, inputPosition);
      if (!hop) {
        addDiagnostic(
            std::move(path), PortalLightDiagnosticReason::SourceBackFacing,
            parent ? parent->strength : 0.0f,
            parent ? parent->visibility : 0.0f);
        continue;
      }
      if (parent && !apertureVisibleThroughPath(
                        *parent, source->aperture)) {
        addDiagnostic(
            std::move(path), PortalLightDiagnosticReason::ApertureInvisible,
            parent->strength, parent->visibility);
        continue;
      }

      PortalLightAttachment candidate = parent ? *parent
                                               : PortalLightAttachment{};
      if (!parent) {
        candidate.sourcePosition = playerTorchPosition;
        candidate.attenuationRadius = playerTorch.attenuationRadius;
        candidate.attenuationFalloff = playerTorch.attenuationFalloff;
        candidate.strength = PlayerTorchRadiance;
        candidate.visibility = 1.0f;
      }
      candidate.position = hop->virtualLightPosition;
      candidate.path = std::move(path);

      auto sourceCentre = glm::vec3{
          hop->sourceApertureCentre.x,
          (hop->sourceApertureBottom + hop->sourceApertureTop) * 0.5f,
          hop->sourceApertureCentre.z};
      auto distance = glm::distance(inputPosition, sourceCentre);
      auto direction = glm::normalize(inputPosition - sourceCentre);
      auto facing = std::max(
          glm::dot(
              glm::normalize(hop->sourceApertureFront), direction),
          0.0f);
      auto area = (hop->apertureHalfWidth * 2.0f) *
                  (hop->sourceApertureTop - hop->sourceApertureBottom);
      auto hopVisibility = std::clamp(
          area * facing /
              std::max(distance * distance, PlaneTolerance),
          0.0f, 1.0f);
      candidate.strength = std::min(
          candidate.strength,
          PlayerTorchRadiance * attenuation(distance, playerTorch));
      candidate.visibility *= hopVisibility;
      candidate.hops.push_back(std::move(*hop));
      candidates.push_back(candidate);
      expand(candidates.back());
    }
  };
  expand(std::nullopt);

  std::ranges::sort(candidates, [](auto const& left, auto const& right) {
    if (left.strength != right.strength) {
      return left.strength > right.strength;
    }
    if (left.visibility != right.visibility) {
      return left.visibility > right.visibility;
    }
    return left.path < right.path;
  });

  std::vector<PortalLightAttachment const*> unique;
  for (auto const& candidate : candidates) {
    auto same = std::ranges::find_if(unique, [&](auto const* retained) {
      return equivalent(candidate, *retained);
    });
    if (same != unique.end()) {
      plan.diagnostics.push_back(
          {candidate.path, PortalLightDiagnosticReason::EquivalentLight,
           candidate.strength, candidate.visibility, (*same)->path});
      continue;
    }
    unique.push_back(&candidate);

    if (plan.lights.size() >= limits.maxVirtualLights) {
      addDiagnostic(
          candidate.path, PortalLightDiagnosticReason::VirtualLightBudget,
          candidate.strength, candidate.visibility);
      continue;
    }
    auto shadowCost = static_cast<uint32_t>(candidate.hops.size() + 1);
    if (plan.shadowPassCount + shadowCost > limits.maxShadowPasses) {
      addDiagnostic(
          candidate.path, PortalLightDiagnosticReason::ShadowPassBudget,
          candidate.strength, candidate.visibility);
      continue;
    }

    plan.shadowPassCount += shadowCost;
    plan.lights.push_back(candidate);
    addDiagnostic(
        candidate.path, PortalLightDiagnosticReason::Retained,
        candidate.strength, candidate.visibility);
  }
  return plan;
}

bool PortalLightAdmitsReceiver(
    PortalLightAttachment const& light,
    glm::vec3 const& receiverPosition) {
  if (light.hops.empty() || light.hops.size() > PortalLightHopLimit) {
    return false;
  }
  auto foldedReceiver = receiverPosition;
  auto foldedLight = light.position;
  for (auto hop = light.hops.rbegin(); hop != light.hops.rend(); ++hop) {
    glm::vec3 intersection;
    if (!gate(*hop, foldedReceiver, foldedLight, intersection)) {
      return false;
    }
    foldedReceiver = glm::vec3(
        hop->destinationToSource * glm::vec4(intersection, 1.0f));
    foldedLight = glm::vec3(
        hop->destinationToSource * glm::vec4(foldedLight, 1.0f));
  }
  return true;
}

bool AttachPortalLightsToPass(
    mpp::ScenePassOverrides& pass,
    std::span<PortalLightShadowAttachment const> lights) {
  if (lights.empty()) return true;
  if (lights.size() > PortalLightAttachmentLimit) return false;

  uint32_t shadowCount{};
  for (auto const& shadow : lights) {
    shadowCount += static_cast<uint32_t>(shadow.shadowMaps.size());
    if (shadow.light.hops.empty() ||
        shadow.light.hops.size() > PortalLightHopLimit ||
        shadow.shadowMaps.size() != shadow.light.hops.size() + 1 ||
        std::ranges::any_of(
            shadow.shadowMaps, [](auto const& map) { return !map; }) ||
        !std::isfinite(shadow.shadowRange) || shadow.shadowRange <= 0.0f ||
        !std::isfinite(shadow.mapTexelSize) ||
        shadow.mapTexelSize <= 0.0f ||
        !std::isfinite(shadow.constantBias) || shadow.constantBias < 0.0f ||
        !std::isfinite(shadow.normalBias) || shadow.normalBias < 0.0f ||
        !std::isfinite(shadow.filterRadiusTexels) ||
        shadow.filterRadiusTexels < 0.0f ||
        !std::isfinite(shadow.fadeStartNormalized) ||
        shadow.fadeStartNormalized < 0.0f ||
        shadow.fadeStartNormalized > 1.0f) {
      return false;
    }
  }
  if (shadowCount > PortalLightShadowPassLimit) return false;

  auto const& first = lights.front();
  if (std::ranges::any_of(lights, [&](auto const& shadow) {
        return shadow.shadowRange != first.shadowRange ||
               shadow.constantBias != first.constantBias ||
               shadow.normalBias != first.normalBias ||
               shadow.filterRadiusTexels != first.filterRadiusTexels ||
               shadow.fadeStartNormalized != first.fadeStartNormalized ||
               shadow.mapTexelSize != first.mapTexelSize ||
               shadow.pcf != first.pcf;
      })) {
    return false;
  }

  std::vector<std::string> uniformNames{
      "PORTAL_LIGHT_COUNT", "PORTAL_LIGHT_SHADOW_PARAMS",
      "PORTAL_LIGHT_SHADOW_BIAS"};
  std::vector<std::string> samplerNames;
  for (size_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex) {
    for (auto field : {"POSITION", "SOURCE_POSITION", "RADIANCE",
                       "HOP_COUNT"}) {
      uniformNames.push_back(lightUniform(field, lightIndex));
    }
    for (size_t hopIndex = 0;
         hopIndex < lights[lightIndex].light.hops.size(); ++hopIndex) {
      for (auto field : {"APERTURE_CENTRE", "APERTURE_TANGENT",
                         "APERTURE_FRONT", "SOURCE_APERTURE_FRONT",
                         "APERTURE_BOUNDS", "DESTINATION_TO_SOURCE"}) {
        uniformNames.push_back(hopUniform(field, lightIndex, hopIndex));
      }
    }
    for (size_t leg = 0;
         leg < lights[lightIndex].shadowMaps.size(); ++leg) {
      samplerNames.push_back(shadowSampler(lightIndex, leg));
    }
  }

  auto const& existing = pass.uniforms.getUniformData();
  if (std::ranges::any_of(uniformNames, [&](auto const& name) {
        return existing.contains(name);
      }) ||
      std::ranges::any_of(samplerNames, [&](auto const& name) {
        return pass.samplers.contains(name);
      })) {
    return false;
  }

  auto overrides = pass.uniforms;
  auto samplers = pass.samplers;
  overrides.setUniform(
      "PORTAL_LIGHT_COUNT", static_cast<int32_t>(lights.size()));
  overrides.setUniform(
      "PORTAL_LIGHT_SHADOW_PARAMS",
      glm::vec4{first.mapTexelSize, first.filterRadiusTexels,
                first.pcf ? 1.0f : 0.0f, first.shadowRange});
  overrides.setUniform(
      "PORTAL_LIGHT_SHADOW_BIAS",
      glm::vec3{first.constantBias, first.normalBias,
                first.fadeStartNormalized});

  for (size_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex) {
    auto const& shadow = lights[lightIndex];
    auto const& light = shadow.light;
    overrides.setUniform(
        lightUniform("POSITION", lightIndex), light.position);
    overrides.setUniform(
        lightUniform("SOURCE_POSITION", lightIndex), light.sourcePosition);
    overrides.setUniform(
        lightUniform("RADIANCE", lightIndex), light.radiance);
    overrides.setUniform(
        lightUniform("HOP_COUNT", lightIndex),
        static_cast<int32_t>(light.hops.size()));
    for (size_t hopIndex = 0; hopIndex < light.hops.size(); ++hopIndex) {
      auto const& hop = light.hops[hopIndex];
      overrides.setUniform(
          hopUniform("APERTURE_CENTRE", lightIndex, hopIndex),
          hop.destinationApertureCentre);
      overrides.setUniform(
          hopUniform("APERTURE_TANGENT", lightIndex, hopIndex),
          hop.destinationApertureTangent);
      overrides.setUniform(
          hopUniform("APERTURE_FRONT", lightIndex, hopIndex),
          hop.destinationApertureFront);
      overrides.setUniform(
          hopUniform("SOURCE_APERTURE_FRONT", lightIndex, hopIndex),
          hop.sourceApertureFront);
      overrides.setUniform(
          hopUniform("APERTURE_BOUNDS", lightIndex, hopIndex),
          glm::vec3{hop.apertureHalfWidth,
                    hop.destinationApertureBottom,
                    hop.destinationApertureTop});
      overrides.setUniform(
          hopUniform("DESTINATION_TO_SOURCE", lightIndex, hopIndex),
          hop.destinationToSource);
    }
    for (size_t leg = 0; leg < shadow.shadowMaps.size(); ++leg) {
      samplers.emplace(
          shadowSampler(lightIndex, leg), shadow.shadowMaps[leg]);
    }
  }

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
