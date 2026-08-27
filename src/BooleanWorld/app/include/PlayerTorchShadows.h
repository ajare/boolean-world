#pragma once

#include <string_view>

#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>

#include "VideoOptions.h"

namespace bw::app {

inline constexpr std::string_view playerTorchShadowDomain =
    "BooleanWorld.PlayerTorch";

inline mpp::ShadowOptions playerTorchMppShadowOptions(
    ShadowOptions const& configured, glm::vec3 const& position) {
  mpp::ShadowOptions result;
  result.enabled = configured.enabled;
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
