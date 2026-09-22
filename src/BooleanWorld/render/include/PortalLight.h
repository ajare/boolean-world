#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>

#include <core/Portal.h>

#include "VideoOptions.h"

// The world shaders declare a fixed attachment array. One-hop lighting never
// grows this list at runtime; an overflowing pass receives no transmitted
// contribution at all.
inline constexpr std::size_t PortalLightAttachmentLimit = 1;
inline constexpr float PlayerTorchRadiance = 14.0f;

struct PortalLightAttachment {
  glm::vec3 position{};
  glm::vec3 radiance{PlayerTorchRadiance};
  glm::vec3 apertureCentre{};
  glm::vec3 apertureTangent{};
  glm::vec3 apertureFront{};
  float apertureHalfWidth{};
  float apertureBottom{};
  float apertureTop{};
  float attenuationRadius{};
  float attenuationFalloff{};
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

// Copies a bounded attachment set into generic auxiliary-pass uniform
// overrides. Failure is atomic; a clean pass therefore remains spill-free.
[[nodiscard]] bool AttachPortalLightsToPass(
    mpp::AuxiliarySceneView& view,
    std::span<PortalLightAttachment const> lights);
