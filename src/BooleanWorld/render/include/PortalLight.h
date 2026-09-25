#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>
#include <mpp/Resource.h>

#include <core/Portal.h>

#include "VideoOptions.h"

// These are three independent work budgets. A path consumes one virtual-light
// slot and one shadow pass for each of its folded segments (hops + 1).
inline constexpr uint32_t PortalLightHopLimit = 3;
inline constexpr std::size_t PortalLightAttachmentLimit = 2;
inline constexpr uint32_t PortalLightShadowPassLimit = 8;
inline constexpr float PlayerTorchRadiance = 14.0f;

// Equivalent paths are compared in renderer-space world units. Positions and
// aperture dimensions within 1e-4, unit axes within 1e-5, and inverse-transform
// elements within 1e-5 are intentionally considered the same virtual light.
inline constexpr float PortalLightPositionTolerance = 1e-4f;
inline constexpr float PortalLightDirectionTolerance = 1e-5f;
inline constexpr float PortalLightTransformTolerance = 1e-5f;

struct PortalLightEndpointKey {
  uint32_t layerId{};
  uint32_t loopId{};
  uint32_t endpointId{};

  auto operator<=>(PortalLightEndpointKey const&) const = default;
};

struct PortalLightPathHop {
  PortalLightEndpointKey sourceEndpoint{};
  PortalLightEndpointKey destinationEndpoint{};
  glm::vec3 virtualLightPosition{};
  glm::vec3 sourceApertureCentre{};
  glm::vec3 sourceApertureTangent{};
  glm::vec3 sourceApertureFront{};
  glm::vec3 destinationApertureCentre{};
  glm::vec3 destinationApertureTangent{};
  glm::vec3 destinationApertureFront{};
  glm::mat4 destinationToSource{1.0f};
  float apertureHalfWidth{};
  float sourceApertureBottom{};
  float sourceApertureTop{};
  float destinationApertureBottom{};
  float destinationApertureTop{};
};

struct PortalLightAttachment {
  glm::vec3 position{};
  glm::vec3 sourcePosition{};
  glm::vec3 radiance{PlayerTorchRadiance};
  float attenuationRadius{};
  float attenuationFalloff{};
  float strength{};
  float visibility{};
  std::vector<PortalLightEndpointKey> path;
  std::vector<PortalLightPathHop> hops;
};

// A contribution is publishable only after every folded segment has a complete
// comparison cubemap. Keeping all maps with the path makes pass attachment
// atomic: there is no partly shadowed or unshadowed Portal-light fallback.
struct PortalLightShadowAttachment {
  PortalLightAttachment light;
  // Ordered from the real-light/source segment to the final receiver segment.
  // A path with N hops therefore owns N + 1 maps.
  std::vector<mpp::ResourcePtr> shadowMaps;
  float shadowRange{};
  float constantBias{};
  float normalBias{};
  float filterRadiusTexels{};
  float fadeStartNormalized{};
  float mapTexelSize{};
  bool pcf{};
};

enum class PortalLightDiagnosticReason : uint8_t {
  Retained,
  Inactive,
  SourceBackFacing,
  ApertureInvisible,
  EndpointCycle,
  EquivalentLight,
  HopBudget,
  VirtualLightBudget,
  ShadowPassBudget,
  ShadowUnavailable,
};

[[nodiscard]] std::string_view PortalLightDiagnosticReasonText(
    PortalLightDiagnosticReason reason);

struct PortalLightDiagnostic {
  std::vector<PortalLightEndpointKey> path;
  PortalLightDiagnosticReason reason{PortalLightDiagnosticReason::Inactive};
  float strength{};
  float visibility{};
  std::vector<PortalLightEndpointKey> equivalentPath;
};

struct PortalLightLimits {
  // Zero disables transmitted light. One preserves one-hop-only behaviour.
  uint32_t maxHops{PortalLightHopLimit};
  std::size_t maxVirtualLights{PortalLightAttachmentLimit};
  uint32_t maxShadowPasses{PortalLightShadowPassLimit};
};

struct PortalLightPlan {
  std::vector<PortalLightAttachment> lights;
  std::vector<PortalLightDiagnostic> diagnostics;
  uint32_t shadowPassCount{};

  [[nodiscard]] uint32_t count(
      PortalLightDiagnosticReason reason) const;
};

// Builds and deterministically ranks every simple endpoint path reachable from
// the real Player Torch. Ranking is descending estimated strength, then
// aperture visibility, then stable authored endpoint identity. Equivalent
// folded paths are removed before the independent light and shadow budgets.
[[nodiscard]] PortalLightPlan PlanPortalLights(
    std::span<bw::core::ResolvedPortalLoop const> loops,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch,
    PortalLightLimits limits = {});

// Builds the destination-side virtual Player Torch for one hop only when the
// real Torch is in front of the source endpoint. This compatibility seam uses
// the same path representation and canonical transform as recursive planning.
// sourceEndpointId is stable authored identity, never an endpoint storage slot.
[[nodiscard]] std::optional<PortalLightAttachment> BuildPortalLightAttachment(
    bw::core::ResolvedPortalLoop const& portalLoop,
    uint32_t sourceEndpointId,
    glm::vec3 const& playerTorchPosition,
    bw::app::PlayerTorchOptions const& playerTorch);

// CPU predicate matching all shader gates. Starting at the receiver, it folds
// the ray backwards through every destination aperture in reverse path order.
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
