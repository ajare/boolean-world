#pragma once

#include <optional>
#include <string_view>

#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>

#include "VideoOptions.h"

namespace bw::app {

inline constexpr std::string_view playerTorchShadowDomain =
    "BooleanWorld.PlayerTorch";

// Kept separately from VideoOptions: F1 adjustments apply only to the live
// shadow domain and must never become configuration writes.
struct PlayerTorchShadowSessionOptions {
  std::optional<bool> enabledOverride;
  ShadowOptions options;
};

inline PlayerTorchShadowSessionOptions playerTorchShadowSessionOptions(
    ShadowOptions const& configured) {
  return {{}, configured};
}

inline bool playerTorchShadowsEnabled(
    ShadowOptions const& configured,
    std::optional<bool> enabledOverride = std::nullopt) {
  return enabledOverride.value_or(configured.enabled);
}

// A disabled domain is a fallback only when a prior enabled request had a
// chance to allocate it. This distinguishes a configured-off domain from a
// session override that is deliberately enabling it for the first time.
inline bool playerTorchShadowHardwareFallbackDetected(
    bool requestedEnabledPreviously, bool requestedEnabledNow,
    bool domainEnabled) {
  return requestedEnabledPreviously && requestedEnabledNow && !domainEnabled;
}

inline mpp::ShadowOptions playerTorchMppShadowOptions(
    ShadowOptions const& configured, glm::vec3 const& position,
    std::optional<bool> enabledOverride = std::nullopt) {
  mpp::ShadowOptions result;
  result.enabled = playerTorchShadowsEnabled(configured, enabledOverride);
  result.light.type = mpp::ShadowLightType::Point;
  result.light.position = position;
  result.light.range = configured.range;
  result.light.lightIndex = 0;
  result.resolution = configured.faceResolution;
  result.nearPlane = configured.nearPlane;
  result.farPlane = configured.range;
  result.constantBias = configured.constantBias;
  result.normalBias = configured.normalBias;
  result.filterMode = configured.filter == ShadowFilter::Hard
                          ? mpp::ShadowFilterMode::Hard
                          : mpp::ShadowFilterMode::Pcf3x3;
  result.filterRadiusTexels = configured.filterRadius;
  result.fadeStartNormalized = configured.fadeStart;
  return result;
}

inline void joinPlayerTorchShadowDomain(mpp::RenderPipelineOptions& pipeline) {
  pipeline.shadowDomain = playerTorchShadowDomain;
}

}  // namespace bw::app
