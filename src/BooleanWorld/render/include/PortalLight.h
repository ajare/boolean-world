#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>
#include <mpp/Resource.h>

#include <core/Portal.h>

#include "VideoOptions.h"

// The world shaders declare a fixed attachment array. One-hop lighting never
// grows this list at runtime; an overflowing pass receives no transmitted
// contribution at all.
inline constexpr std::size_t PortalLightAttachmentLimit = 1;
inline constexpr float PlayerTorchRadiance = 14.0f;

struct PortalLightAttachment {
  glm::vec3 position{};
  glm::vec3 sourcePosition{};
  glm::vec3 radiance{PlayerTorchRadiance};
  glm::vec3 apertureCentre{};
  glm::vec3 apertureTangent{};
  glm::vec3 apertureFront{};
  glm::vec3 sourceApertureCentre{};
  glm::vec3 sourceApertureTangent{};
  glm::vec3 sourceApertureFront{};
  glm::mat4 destinationToSource{1.0f};
  float apertureHalfWidth{};
  float apertureBottom{};
  float apertureTop{};
  float sourceApertureBottom{};
  float sourceApertureTop{};
  float attenuationRadius{};
  float attenuationFalloff{};
};

// A contribution is publishable only after both legs have complete comparison
// cubemaps. Keeping the maps and their sampling parameters together makes
// pass attachment atomic: there is no unshadowed Portal-light fallback.
struct PortalLightShadowAttachment {
  PortalLightAttachment light;
  mpp::ResourcePtr sourceShadowMap;
  mpp::ResourcePtr destinationShadowMap;
  float shadowRange{};
  float constantBias{};
  float normalBias{};
  float filterRadiusTexels{};
  float fadeStartNormalized{};
  float mapTexelSize{};
  bool pcf{};
};

// Builds the destination-side virtual Player Torch only when the real Torch is
// in front of the source endpoint. Position and aperture data come from the
// canonical generated Portal transform; authored width never scales radiance.
[[nodiscard]] std::optional<PortalLightAttachment> BuildPortalLightAttachment(
    bw::core::ResolvedPortalPair const& pair,
    uint32_t sourceEndpoint,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch);

// CPU predicate matching the shader gate. The receiver-to-virtual-light
// segment must cross the destination plane inside the resolved rectangle,
// from its retained front side.
[[nodiscard]] bool PortalLightAdmitsReceiver(
    PortalLightAttachment const& light,
    glm::vec3 const& receiverPosition);

// Copies a bounded, fully shadowed attachment set into pass-owned uniform and
// sampler overrides. Failure is atomic; a clean pass therefore remains
// spill-free and the ordinary Player Torch shadow domain stays independently
// active. Both primary and Auxiliary views use this same contract.
[[nodiscard]] bool AttachPortalLightsToPass(
    mpp::ScenePassOverrides& pass,
    std::span<PortalLightShadowAttachment const> lights);
[[nodiscard]] bool AttachPortalLightsToPass(
    mpp::AuxiliarySceneView& view,
    std::span<PortalLightShadowAttachment const> lights);
