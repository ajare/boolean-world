#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <mpp/RenderPipeline.h>

#include <core/Portal.h>

struct PortalEndpointKey {
  uint32_t layerId{};
  uint32_t pairId{};
  uint8_t endpointId{};

  auto operator<=>(PortalEndpointKey const&) const = default;
};

struct SelectedPortalView {
  PortalEndpointKey key{};
  bw::core::ResolvedPortalPair const* pair{};
  uint32_t sourceEndpoint{};
  float projectedCoverage{};
  float cameraDistance{};
};

// Selects the single front-facing, frustum-visible resolved aperture that gets
// a live view this frame. Coverage, distance, and stable authored identity are
// strict deterministic ordering keys, in that order.
[[nodiscard]] std::optional<SelectedPortalView> SelectPortalView(
    std::span<bw::core::ResolvedPortalPair const> pairs,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition);

struct BuiltPortalView {
  mpp::AuxiliarySceneView auxiliary;
  // Projects source-aperture renderer-space positions into the auxiliary
  // image. Sampling divides by W; local aperture UVs are never involved.
  glm::mat4 sourceProjectiveTransform{1.0f};
  glm::mat4 sourceToDestination{1.0f};
};

// Transforms the observing camera through BuildPortalRigidTransform, preserving
// its exact projection/aspect. The destination plane keeps its front half-space
// and receives MPP's world-unit oblique-clipping seam bias.
[[nodiscard]] BuiltPortalView BuildPortalView(
    SelectedPortalView const& selected,
    glm::mat4 const& observingView,
    glm::mat4 const& observingProjection,
    float nearDistance,
    float farDistance,
    uint32_t width,
    uint32_t height);
