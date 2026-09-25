#include "PortalView.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
using ClipPolygon = std::vector<glm::vec4>;

float planeDistance(glm::vec4 const& point, int plane) {
  switch (plane) {
    case 0:
      return point.x + point.w;
    case 1:
      return point.w - point.x;
    case 2:
      return point.y + point.w;
    case 3:
      return point.w - point.y;
    case 4:
      return point.z + point.w;
    case 5:
      return point.w - point.z;
    default:
      return point.w - 1e-6f;
  }
}

ClipPolygon clipToFrustum(ClipPolygon polygon, bool clampNearPlane) {
  // Clip the eye plane first. Near-clamped apertures can straddle it when
  // approached obliquely; perspective division must never see w == 0.
  for (int plane = 6; plane >= 0 && !polygon.empty(); --plane) {
    if (clampNearPlane && plane == 4) continue;
    ClipPolygon clipped;
    clipped.reserve(polygon.size() + 1);
    auto previous = polygon.back();
    auto previousDistance = planeDistance(previous, plane);
    for (auto const& current : polygon) {
      auto currentDistance = planeDistance(current, plane);
      auto previousInside = previousDistance >= 0.0f;
      auto currentInside = currentDistance >= 0.0f;
      if (previousInside != currentInside) {
        auto denominator = previousDistance - currentDistance;
        if (std::abs(denominator) > std::numeric_limits<float>::epsilon()) {
          clipped.push_back(
              previous + (previousDistance / denominator) *
                             (current - previous));
        }
      }
      if (currentInside) clipped.push_back(current);
      previous = current;
      previousDistance = currentDistance;
    }
    polygon = std::move(clipped);
  }
  return polygon;
}

glm::vec3 rendererPosition(wp::Vector2 const& position, float elevation) {
  return {position.x, elevation, -position.y};
}

struct ProjectedAperture {
  bool inFrustum{};
  float area{};
};

ProjectedAperture projectAperture(
    bw::core::ResolvedAperture const& aperture,
    glm::mat4 const& viewProjection, bool clampNearPlane, bool coplanar) {
  auto half = aperture.tangent * (aperture.width * 0.5f);
  auto left = aperture.centre - half;
  auto right = aperture.centre + half;
  std::array<glm::vec3, 4> corners{
      rendererPosition(left, aperture.bottom),
      rendererPosition(right, aperture.bottom),
      rendererPosition(right, aperture.top),
      rendererPosition(left, aperture.top)};

  ClipPolygon polygon;
  polygon.reserve(corners.size());
  for (auto const& corner : corners) {
    auto projected = viewProjection * glm::vec4(
        corner - glm::vec3{aperture.front.x, 0.0f, -aperture.front.y} *
            (clampNearPlane && coplanar ? 0.0001f : 0.0f), 1.0f);
    // Portal surfaces must survive the near plane until the eye crosses.
    // Match the depth-clamped aperture rasterization in world.vert.
    polygon.push_back(projected);
  }
  polygon = clipToFrustum(std::move(polygon), clampNearPlane);
  if (polygon.size() < 3) return {};

  float twiceArea = 0.0f;
  for (size_t index = 0; index < polygon.size(); ++index) {
    auto const& first = polygon[index];
    auto const& second = polygon[(index + 1) % polygon.size()];
    if (first.w <= 0.0f || second.w <= 0.0f) return {};
    auto a = glm::vec2(first) / first.w;
    auto b = glm::vec2(second) / second.w;
    twiceArea += a.x * b.y - b.x * a.y;
  }
  auto area = std::abs(twiceArea) * 0.5f;
  return {std::isfinite(area), std::isfinite(area) ? area : 0.0f};
}

glm::vec3 cameraPosition(glm::mat4 const& view) {
  auto inverse = glm::inverse(view);
  return glm::vec3(inverse[3]) / inverse[3].w;
}

struct CandidateEvaluation {
  std::optional<SelectedPortalView> selected;
  PortalViewDiagnostic diagnostic;
};

std::vector<CandidateEvaluation> evaluateCandidates(
    std::span<bw::core::ResolvedPortalLoop const> loops,
    glm::mat4 const& viewProjection,
    glm::vec3 const& position,
    uint32_t recursionDepth,
    float minimumCoverage,
    PortalOcclusionQuery const& occluded) {
  std::vector<CandidateEvaluation> result;
  auto cameraWorldPlane = wp::Vector2{position.x, -position.z};

  for (auto const& portalLoop : loops) {
    for (auto const& endpoint : portalLoop.endpoints) {
      PortalEndpointKey key{portalLoop.layerId, endpoint.endpointId};
      PortalViewDiagnostic diagnostic;
      diagnostic.endpoint = key;
      diagnostic.recursionDepth = recursionDepth;

      if (!portalLoop.active || !endpoint.resolved) {
        diagnostic.cutoff = PortalViewCutoffReason::Inactive;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }

      auto const& aperture = endpoint.aperture;
      auto side = aperture.front.dot(cameraWorldPlane - aperture.centre);
      if (side < -1e-5f) {
        diagnostic.cutoff = PortalViewCutoffReason::VisibilityBackFacing;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }

      // A camera exactly on the aperture belongs to its front view only
      // while inside the rectangle and looking through it, not away from it.
      if (side <= 1e-5f) {
        auto offset = cameraWorldPlane - aperture.centre;
        auto intoPortal = viewProjection * glm::vec4{
            -aperture.front.x, 0.0f, aperture.front.y, 0.0f};
        if (std::abs(offset.dot(aperture.tangent)) >= aperture.width * 0.5f ||
            position.y <= aperture.bottom || position.y >= aperture.top ||
            intoPortal.w <= 0.0f) {
          diagnostic.cutoff = PortalViewCutoffReason::VisibilityBackFacing;
          result.push_back({std::nullopt, diagnostic});
          continue;
        }
      }
      // Auxiliary near planes are destination clipping planes: never relax
      // them or the exit wall can reappear inside the virtual view.
      auto projected = projectAperture(
          aperture, viewProjection, recursionDepth == 1, side <= 1e-5f);
      if (!projected.inFrustum) {
        diagnostic.cutoff = PortalViewCutoffReason::VisibilityOutOfFrustum;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }
      diagnostic.projectedCoverage = projected.area;
      if (projected.area < minimumCoverage) {
        diagnostic.cutoff = PortalViewCutoffReason::ProjectedArea;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }
      if (occluded && occluded(key, aperture, viewProjection)) {
        diagnostic.cutoff = PortalViewCutoffReason::VisibilityOccluded;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }

      auto centre = rendererPosition(
          aperture.centre, (aperture.bottom + aperture.top) * 0.5f);
      auto distance = glm::distance(position, centre);
      if (!std::isfinite(distance)) {
        diagnostic.cutoff = PortalViewCutoffReason::VisibilityOutOfFrustum;
        result.push_back({std::nullopt, diagnostic});
        continue;
      }
      diagnostic.cameraDistance = distance;
      result.push_back({SelectedPortalView{
                            key, &portalLoop, projected.area, distance},
                        diagnostic});
    }
  }
  return result;
}

using MatrixBits = std::array<uint32_t, 16>;

MatrixBits matrixBits(glm::mat4 const& matrix) {
  MatrixBits result{};
  size_t output = 0;
  for (size_t column = 0; column < 4; ++column) {
    for (size_t row = 0; row < 4; ++row) {
      result[output++] = std::bit_cast<uint32_t>(matrix[column][row]);
    }
  }
  return result;
}

struct CameraStateKey {
  MatrixBits view{};
  MatrixBits projection{};
  std::array<uint32_t, 4> clipPlane{};
  uint32_t width{};
  uint32_t height{};
  uint32_t recursionDepth{};

  auto operator<=>(CameraStateKey const&) const = default;
};

CameraStateKey cameraStateKey(
    mpp::AuxiliarySceneView const& view, uint32_t recursionDepth) {
  CameraStateKey result;
  result.view = matrixBits(view.view);
  result.projection = matrixBits(view.projection);
  for (size_t index = 0; index < 4; ++index) {
    result.clipPlane[index] =
        std::bit_cast<uint32_t>(view.worldClipPlane[index]);
  }
  result.width = view.width;
  result.height = view.height;
  result.recursionDepth = recursionDepth;
  return result;
}
}  // namespace

std::optional<SelectedPortalView> SelectPortalView(
    std::span<bw::core::ResolvedPortalLoop const> loops,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPositionValue) {
  auto evaluated = evaluateCandidates(
      loops, viewProjection, cameraPositionValue, 1, 1e-8f, {});
  std::vector<SelectedPortalView> candidates;
  for (auto const& item : evaluated) {
    if (item.selected) candidates.push_back(*item.selected);
  }
  std::ranges::sort(candidates, [](auto const& left, auto const& right) {
    if (left.projectedCoverage != right.projectedCoverage) {
      return left.projectedCoverage > right.projectedCoverage;
    }
    if (left.cameraDistance != right.cameraDistance) {
      return left.cameraDistance < right.cameraDistance;
    }
    return left.key < right.key;
  });
  return candidates.empty() ? std::nullopt
                            : std::optional<SelectedPortalView>{candidates[0]};
}

BuiltPortalView BuildPortalView(
    SelectedPortalView const& selected,
    glm::mat4 const& observingView,
    glm::mat4 const& observingProjection,
    float nearDistance,
    float farDistance,
    uint32_t width,
    uint32_t height) {
  if (!selected.loop || !selected.loop->active || width == 0 || height == 0) {
    throw std::invalid_argument(
        "A Portal view requires an active selection and non-zero dimensions");
  }

  auto rigid = bw::core::BuildPortalMapping(
      *selected.loop,
      selected.key.endpointId);
  auto sourceToDestination = glm::make_mat4(rigid.rendererMatrix().data());
  auto destinationView = observingView * glm::inverse(sourceToDestination);
  auto const& destination = rigid.destination;
  auto destinationNormal = glm::vec3{
      destination.front.x, 0.0f, -destination.front.y};
  auto destinationCentre = rendererPosition(
      destination.centre, destination.bottom);
  auto clipPlane = glm::vec4{
      destinationNormal,
      -glm::dot(destinationNormal, destinationCentre) -
          mpp::AuxiliaryViewClipSeamBias};
  // Clip inside the destination's front side. Expanding behind the plane
  // admits the opaque back face of the aperture itself into the virtual view.
  auto clipped = mpp::buildObliquelyClippedVirtualCamera(
      destinationView, observingProjection, clipPlane, 0.0f);

  BuiltPortalView result;
  result.sourceToDestination = sourceToDestination;
  result.reversesHandedness = rigid.reversesHandedness();
  result.sourceProjectiveTransform =
      clipped.projection * destinationView * sourceToDestination;
  result.auxiliary.slot = "Portal0";
  result.auxiliary.view = destinationView;
  result.auxiliary.projection = observingProjection;
  result.auxiliary.worldClipPlane = clipPlane;
  result.auxiliary.width = width;
  result.auxiliary.height = height;
  result.auxiliary.nearDistance = nearDistance;
  result.auxiliary.farDistance = farDistance;
  result.auxiliary.seamBias = 0.0f;
  // Winding is camera parity, not just the latest hop: two reflections
  // restore the original handedness, including across ordinary Portal hops.
  result.auxiliary.reverseWinding = glm::determinant(glm::mat3(destinationView)) < 0.0f;
  return result;
}

std::string_view PortalViewCutoffReasonText(PortalViewCutoffReason reason) {
  switch (reason) {
    case PortalViewCutoffReason::None:
      return "none";
    case PortalViewCutoffReason::Inactive:
      return "inactive";
    case PortalViewCutoffReason::VisibilityBackFacing:
      return "visibility-back-facing";
    case PortalViewCutoffReason::VisibilityOutOfFrustum:
      return "visibility-out-of-frustum";
    case PortalViewCutoffReason::VisibilityOccluded:
      return "visibility-occluded";
    case PortalViewCutoffReason::ProjectedArea:
      return "projected-area";
    case PortalViewCutoffReason::RecursionDepth:
      return "recursion-depth";
    case PortalViewCutoffReason::TargetBudget:
      return "target-budget";
    case PortalViewCutoffReason::FrameBudget:
      return "frame-budget";
  }
  return "unknown";
}

uint32_t PortalViewPlan::cutoffCount(PortalViewCutoffReason reason) const {
  return static_cast<uint32_t>(std::ranges::count_if(
      diagnostics, [&](auto const& item) { return item.cutoff == reason; }));
}

PortalViewPlanner::PortalViewPlanner(PortalViewLimits limits)
    : mLimits(limits) {
  mLimits.maxTargets = std::min(mLimits.maxTargets, PortalViewSlotCount);
  mLimits.selectionHysteresis =
      std::max(0.0f, mLimits.selectionHysteresis);
  mLimits.minimumProjectedCoverage =
      std::max(0.0f, mLimits.minimumProjectedCoverage);
}

void PortalViewPlanner::resetHistory() {
  mPreviousSlots.clear();
  mPreviouslySelected.clear();
}

PortalViewPlan PortalViewPlanner::build(
    std::span<bw::core::ResolvedPortalLoop const> loops,
    glm::mat4 const& primaryView,
    glm::mat4 const& primaryProjection,
    float nearDistance,
    float farDistance,
    uint32_t width,
    uint32_t height,
    PortalOcclusionQuery const& occluded) {
  if (width == 0 || height == 0) {
    throw std::invalid_argument("Portal view planning requires non-zero dimensions");
  }

  PortalViewPlan plan;
  std::array<bool, PortalViewSlotCount> usedSlots{};
  std::map<CameraStateKey, uint32_t> equivalentStates;
  std::map<BranchPath, uint32_t> currentSlots;
  std::map<BranchPath, bool> currentlySelected;
  std::vector<BranchPath> nodePaths;

  auto chooseSlot = [&](BranchPath const& path) -> std::optional<uint32_t> {
    auto previous = mPreviousSlots.find(path);
    if (previous != mPreviousSlots.end() &&
        previous->second < mLimits.maxTargets &&
        !usedSlots[previous->second]) {
      usedSlots[previous->second] = true;
      return previous->second;
    }
    for (uint32_t slot = 0; slot < mLimits.maxTargets; ++slot) {
      if (!usedSlots[slot]) {
        usedSlots[slot] = true;
        return slot;
      }
    }
    return std::nullopt;
  };

  std::function<void(
      std::optional<uint32_t>, glm::mat4 const&, glm::mat4 const&,
      glm::mat4 const&, glm::vec3 const&, uint32_t, BranchPath const&)>
      expand;
  expand = [&](std::optional<uint32_t> parentNode,
               glm::mat4 const& observingView,
               glm::mat4 const& observingProjection,
               glm::mat4 const& visibilityViewProjection,
               glm::vec3 const& observingPosition,
               uint32_t parentDepth,
               BranchPath const& parentPath) {
    auto evaluations = evaluateCandidates(
        loops, visibilityViewProjection, observingPosition, parentDepth + 1,
        mLimits.minimumProjectedCoverage, occluded);

    struct Eligible {
      SelectedPortalView selected;
      PortalViewDiagnostic diagnostic;
      BranchPath path;
      bool retained{};
    };
    std::vector<Eligible> eligible;
    for (auto& evaluation : evaluations) {
      if (!evaluation.selected) {
        plan.diagnostics.push_back(evaluation.diagnostic);
        continue;
      }
      auto path = parentPath;
      path.push_back(evaluation.selected->key);
      auto retained = mPreviouslySelected.contains(path);
      eligible.push_back(
          {*evaluation.selected, evaluation.diagnostic, std::move(path), retained});
    }

    std::ranges::sort(eligible, [&](Eligible const& left, Eligible const& right) {
      auto leftCoverage = left.selected.projectedCoverage *
                          (left.retained ? 1.0f + mLimits.selectionHysteresis
                                         : 1.0f);
      auto rightCoverage = right.selected.projectedCoverage *
                           (right.retained ? 1.0f + mLimits.selectionHysteresis
                                           : 1.0f);
      if (leftCoverage != rightCoverage) return leftCoverage > rightCoverage;
      if (left.selected.cameraDistance != right.selected.cameraDistance) {
        return left.selected.cameraDistance < right.selected.cameraDistance;
      }
      return left.selected.key < right.selected.key;
    });

    std::vector<PortalViewPlanEdge> edges;
    std::vector<uint32_t> newNodes;
    if (parentDepth >= mLimits.maxRecursionDepth) {
      for (auto& candidate : eligible) {
        candidate.diagnostic.cutoff = PortalViewCutoffReason::RecursionDepth;
        plan.diagnostics.push_back(candidate.diagnostic);
      }
    } else {
      for (auto& candidate : eligible) {
        auto built = BuildPortalView(
            candidate.selected, observingView, observingProjection,
            nearDistance, farDistance, width, height);
        auto depth = parentDepth + 1;
        auto state = cameraStateKey(built.auxiliary, depth);
        auto equivalent = equivalentStates.find(state);
        uint32_t childNode{};
        if (equivalent != equivalentStates.end()) {
          childNode = equivalent->second;
        } else {
          if (plan.nodes.size() >= mLimits.maxRenderedPasses) {
            candidate.diagnostic.cutoff = PortalViewCutoffReason::FrameBudget;
            plan.diagnostics.push_back(candidate.diagnostic);
            continue;
          }
          auto slot = chooseSlot(candidate.path);
          if (!slot) {
            candidate.diagnostic.cutoff = PortalViewCutoffReason::TargetBudget;
            plan.diagnostics.push_back(candidate.diagnostic);
            continue;
          }

          built.auxiliary.slot = "Portal" + std::to_string(*slot);
          auto clipped = mpp::buildObliquelyClippedVirtualCamera(
              built.auxiliary.view, built.auxiliary.projection,
              built.auxiliary.worldClipPlane, built.auxiliary.seamBias);
          childNode = static_cast<uint32_t>(plan.nodes.size());
          plan.nodes.push_back({*slot,
                                depth,
                                built.auxiliary,
                                clipped.projection * clipped.view,
                                cameraPosition(clipped.view),
                                {}});
          nodePaths.push_back(candidate.path);
          equivalentStates.emplace(std::move(state), childNode);
          newNodes.push_back(childNode);
        }

        auto slot = plan.nodes[childNode].slot;
        currentSlots[candidate.path] = slot;
        currentlySelected[candidate.path] = true;
        candidate.diagnostic.selected = true;
        candidate.diagnostic.slot = slot;
        candidate.diagnostic.cutoff = PortalViewCutoffReason::None;
        plan.diagnostics.push_back(candidate.diagnostic);
        edges.push_back({candidate.selected.key, childNode,
                         built.sourceProjectiveTransform,
                         candidate.selected.projectedCoverage,
                         candidate.selected.cameraDistance});
      }
    }

    if (parentNode) {
      plan.nodes[*parentNode].children = std::move(edges);
    } else {
      plan.rootChildren = std::move(edges);
    }

    for (auto nodeIndex : newNodes) {
      auto const node = plan.nodes[nodeIndex];
      expand(
          nodeIndex, node.auxiliary.view, node.auxiliary.projection,
          node.visibilityViewProjection, node.cameraPosition,
          node.recursionDepth, nodePaths[nodeIndex]);
    }
  };

  expand(
      std::nullopt, primaryView, primaryProjection,
      primaryProjection * primaryView, cameraPosition(primaryView), 0, {});

  std::set<uint32_t> visited;
  std::function<void(uint32_t)> appendDeepestFirst = [&](uint32_t nodeIndex) {
    if (!visited.insert(nodeIndex).second) return;
    for (auto const& edge : plan.nodes[nodeIndex].children) {
      appendDeepestFirst(edge.childNode);
    }
    plan.deepestFirst.push_back(nodeIndex);
  };
  for (auto const& edge : plan.rootChildren) {
    appendDeepestFirst(edge.childNode);
  }
  plan.renderedPassCount = static_cast<uint32_t>(plan.nodes.size());

  mPreviousSlots = std::move(currentSlots);
  mPreviouslySelected = std::move(currentlySelected);
  return plan;
}
