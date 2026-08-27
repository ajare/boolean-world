#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "PlayerTorchShadows.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

std::string read(std::filesystem::path const& path) {
  std::ifstream stream(path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

void transfersConfigurationToMpp() {
  bw::app::ShadowOptions configured{
      false, 1537, 87.25f, 0.625f, 0.00125f, 0.00475f,
      bw::app::ShadowFilter::Hard, 2.5f, 0.675f};
  glm::vec3 position{11.5f, 7.25f, -19.0f};
  auto options = bw::app::playerTorchMppShadowOptions(configured, position);
  require(!options.enabled && options.light.type == mpp::ShadowLightType::Point,
          "Player Torch did not configure a disabled point-shadow domain.");
  require(options.light.position == position && options.light.range == 87.25f &&
              options.light.lightIndex == 0,
          "Player Torch position/range/light association was not retained.");
  require(options.resolution == 1537 && options.nearPlane == 0.625f &&
              options.farPlane == 87.25f &&
              options.constantBias == 0.00125f &&
              options.normalBias == 0.00475f &&
              options.filterMode == mpp::ShadowFilterMode::Hard &&
              options.filterRadiusTexels == 2.5f &&
              options.fadeStartNormalized == 0.675f,
          "Configured shadow fields were truncated or reordered for MPP.");
}

void sessionOverrideIsTemporaryAndHonoursFallback() {
  bw::app::ShadowOptions configured;
  configured.enabled = false;
  configured.faceResolution = 1537;
  auto session = bw::app::playerTorchShadowSessionOptions(configured);

  require(!bw::app::playerTorchShadowsEnabled(
              session.options, session.enabledOverride),
          "The configured disabled state was not used without an override.");
  session.enabledOverride = true;
  require(bw::app::playerTorchShadowsEnabled(
              session.options, session.enabledOverride),
          "The enable override did not temporarily enable the domain.");
  session.enabledOverride = false;
  require(!bw::app::playerTorchShadowsEnabled(
              session.options, session.enabledOverride),
          "The disable override did not temporarily disable the domain.");
  require(!configured.enabled && configured.faceResolution == 1537,
          "Session controls changed the configured shadow options.");

  require(!bw::app::playerTorchShadowHardwareFallbackDetected(
              false, true, false),
          "The first configured-off override attempt was mistaken for a fallback.");
  require(bw::app::playerTorchShadowHardwareFallbackDetected(
              true, true, false),
          "A failed enabled request did not retain the hardware fallback.");
  require(!bw::app::playerTorchShadowHardwareFallbackDetected(
              true, false, false),
          "An intentional disable was mistaken for a hardware fallback.");
}

void everyPipelineVariantSharesOneDomain() {
  for (auto scale : bw::app::allRenderScales) {
    (void)scale;
    for (auto aa : bw::app::allAntiAliasingOptions) {
      (void)aa;
      mpp::RenderPipelineOptions pipeline;
      bw::app::joinPlayerTorchShadowDomain(pipeline);
      require(pipeline.shadowDomain == bw::app::playerTorchShadowDomain,
              "A world pipeline variant did not join the shared domain.");
    }
  }
}

void releaseShadersReceiveOnlyDirectTorchVisibility() {
  auto shaderRoot = std::filesystem::path(BW_APP_RESOURCE_DIR) / "shaders";
  for (auto name : {"world_pbr.frag", "world_pbr_2d.frag"}) {
    auto shader = read(shaderRoot / name);
    require(shader.find("samplerCubeShadow POINT_SHADOW_MAP") != std::string::npos &&
                shader.find("uniform ShadowFrame") != std::string::npos &&
                shader.find("playerTorchVisibility") != std::string::npos,
            std::string(name) + " does not declare the point-shadow receiver contract.");
    auto direct = shader.find("direct *= playerTorchVisibility");
    auto ambient = shader.find("ambient", direct);
    require(direct != std::string::npos && ambient != std::string::npos,
            std::string(name) + " does not apply visibility before its independent ambient term.");
    require(shader.find("vec4(value, @In(COLOUR).a)") != std::string::npos,
            std::string(name) + " loses alpha for a blended receiving surface.");
  }
}

void f1ExposesSessionOnlyDiagnostics() {
  auto appRoot = std::filesystem::path(BW_APP_RESOURCE_DIR).parent_path();
  auto state = read(appRoot / "src" / "StatePlayBooleanWorld.cpp");
  for (auto label : {"{Key::F1}", "Enable override",
                     "Range##PlayerTorch", "Constant bias##PlayerTorch",
                     "Normal bias##PlayerTorch", "Filter##PlayerTorch",
                     "PCF radius##PlayerTorch", "Fade start##PlayerTorch",
                     "Cubemap resolution (configured)"}) {
    require(state.find(label) != std::string::npos,
            std::string("F1 diagnostics are missing ") + label + ".");
  }
  require(state.find("faceResolution") == std::string::npos ||
              state.find("&sessionShadows.options.faceResolution") ==
                  std::string::npos,
          "F1 exposes a mutable cubemap-resolution control.");
}
}  // namespace

int main() {
  try {
    transfersConfigurationToMpp();
    sessionOverrideIsTemporaryAndHonoursFallback();
    everyPipelineVariantSharesOneDomain();
    releaseShadersReceiveOnlyDirectTorchVisibility();
    f1ExposesSessionOnlyDiagnostics();
    std::cout << "Player Torch shadow integration tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
