#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// Selects the highest-ranked front-facing, frustum-visible resolved aperture.
// Coverage, distance, and stable authored identity are deterministic ordering
// keys, in that order. Multi-view planning below uses the same evaluation.
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

inline constexpr uint32_t PortalViewSlotCount = 8;

enum class PortalViewCutoffReason : uint8_t {
  None,
  Inactive,
  VisibilityBackFacing,
  VisibilityOutOfFrustum,
  VisibilityOccluded,
  ProjectedArea,
  RecursionDepth,
  TargetBudget,
  FrameBudget,
};

[[nodiscard]] std::string_view PortalViewCutoffReasonText(
    PortalViewCutoffReason reason);

struct PortalViewLimits {
  // Primary view is depth zero; its auxiliary children begin at depth one.
  uint32_t maxRecursionDepth{3};
  uint32_t maxTargets{PortalViewSlotCount};
  uint32_t maxRenderedPasses{PortalViewSlotCount};
  // Projected area is measured in NDC, whose complete viewport has area four.
  float minimumProjectedCoverage{0.0001f};
  // A branch selected last frame receives this fractional coverage preference.
  float selectionHysteresis{0.1f};
};

using PortalOcclusionQuery = std::function<bool(
    PortalEndpointKey const&,
    bw::core::ResolvedAperture const&,
    glm::mat4 const&)>;

struct PortalViewDiagnostic {
  PortalEndpointKey endpoint{};
  uint32_t recursionDepth{};
  float projectedCoverage{};
  float cameraDistance{};
  bool selected{};
  std::optional<uint32_t> slot;
  PortalViewCutoffReason cutoff{PortalViewCutoffReason::None};
};

struct PortalViewPlanEdge {
  PortalEndpointKey endpoint{};
  uint32_t childNode{};
  glm::mat4 sourceProjectiveTransform{1.0f};
  float projectedCoverage{};
  float cameraDistance{};
};

struct PortalViewPlanNode {
  uint32_t slot{};
  uint32_t recursionDepth{};
  mpp::AuxiliarySceneView auxiliary;
  // The actual obliquely-clipped projection used to select this view's
  // children. `auxiliary.projection` remains the exact observing projection.
  glm::mat4 visibilityViewProjection{1.0f};
  glm::vec3 cameraPosition{};
  std::vector<PortalViewPlanEdge> children;
};

struct PortalViewPlan {
  std::vector<PortalViewPlanNode> nodes;
  std::vector<PortalViewPlanEdge> rootChildren;
  // Node indices in dependency order: every child precedes every parent.
  std::vector<uint32_t> deepestFirst;
  std::vector<PortalViewDiagnostic> diagnostics;
  uint32_t renderedPassCount{};

  [[nodiscard]] uint32_t cutoffCount(PortalViewCutoffReason reason) const;
};

// Stateful only for stable branch-to-slot assignment and selection hysteresis.
// Every build creates an acyclic, finite per-frame DAG. A repeated endpoint is
// legal; only explicit limits terminate it. Exact camera states at the same
// depth can share a completed node, while ancestors never can because depth is
// part of the state identity.
class PortalViewPlanner {
public:
  explicit PortalViewPlanner(PortalViewLimits limits = {});

  [[nodiscard]] PortalViewPlan build(
      std::span<bw::core::ResolvedPortalPair const> pairs,
      glm::mat4 const& primaryView,
      glm::mat4 const& primaryProjection,
      float nearDistance,
      float farDistance,
      uint32_t width,
      uint32_t height,
      PortalOcclusionQuery const& occluded = {});

  [[nodiscard]] PortalViewLimits const& limits() const { return mLimits; }
  void resetHistory();

private:
  using BranchPath = std::vector<PortalEndpointKey>;

  PortalViewLimits mLimits;
  std::map<BranchPath, uint32_t> mPreviousSlots;
  std::map<BranchPath, bool> mPreviouslySelected;
};
