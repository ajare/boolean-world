#include "LiquidReflectionSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace bw::app {
namespace {
constexpr float minimumProjectedArea = 1e-8f;

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
    default:
      return point.w - point.z;
  }
}

ClipPolygon clipToFrustum(ClipPolygon polygon) {
  for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
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
          auto amount = previousDistance / denominator;
          clipped.push_back(previous + amount * (current - previous));
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

float projectedArea(
    LiquidSurfaceTriangle const& triangle,
    glm::mat4 const& viewProjection) {
  ClipPolygon polygon;
  polygon.reserve(3);
  for (auto const& vertex : triangle.vertices) {
    polygon.push_back(viewProjection * glm::vec4(vertex, 1.0f));
  }
  polygon = clipToFrustum(std::move(polygon));
  if (polygon.size() < 3) return 0.0f;

  float twiceArea = 0.0f;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    auto const& first = polygon[index];
    auto const& second = polygon[(index + 1) % polygon.size()];
    if (first.w <= 0.0f || second.w <= 0.0f) return 0.0f;
    auto firstX = first.x / first.w;
    auto firstY = first.y / first.w;
    auto secondX = second.x / second.w;
    auto secondY = second.y / second.w;
    twiceArea += firstX * secondY - secondX * firstY;
  }
  return std::abs(twiceArea) * 0.5f;
}

struct VisibleTriangle {
  float elevation;
  float coverage;
  float distance;
};

struct ElevationGroup {
  float minimumElevation;
  float maximumElevation;
  float weightedElevation;
  float coverage;
  float distance;
};
}  // namespace

namespace {
std::vector<SelectedLiquidSurface> rankedVisibleLiquidSurfaces(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition) {
  std::vector<VisibleTriangle> visible;
  visible.reserve(triangles.size());
  for (auto const& triangle : triangles) {
    auto coverage = projectedArea(triangle, viewProjection);
    if (!std::isfinite(coverage) || coverage <= minimumProjectedArea ||
        !std::isfinite(triangle.elevation)) {
      continue;
    }
    auto centre =
        (triangle.vertices[0] + triangle.vertices[1] + triangle.vertices[2]) /
        3.0f;
    auto distance = glm::distance(cameraPosition, centre);
    if (!std::isfinite(distance)) continue;
    visible.push_back({triangle.elevation, coverage, distance});
  }

  std::ranges::sort(
      visible, {}, [](VisibleTriangle const& triangle) {
        return triangle.elevation;
      });

  std::vector<ElevationGroup> groups;
  for (auto const& triangle : visible) {
    if (groups.empty() ||
        triangle.elevation - groups.back().minimumElevation >
            liquidElevationGroupingTolerance) {
      groups.push_back(
          {triangle.elevation, triangle.elevation,
           triangle.elevation * triangle.coverage, triangle.coverage,
           triangle.distance});
      continue;
    }
    auto& group = groups.back();
    group.maximumElevation = triangle.elevation;
    group.weightedElevation += triangle.elevation * triangle.coverage;
    group.coverage += triangle.coverage;
    group.distance = std::min(group.distance, triangle.distance);
  }

  std::ranges::sort(groups, [](ElevationGroup const& left,
                               ElevationGroup const& right) {
    if (left.coverage != right.coverage) {
      return left.coverage > right.coverage;
    }
    if (left.distance != right.distance) {
      return left.distance < right.distance;
    }
    return left.minimumElevation < right.minimumElevation;
  });

  std::vector<SelectedLiquidSurface> ranked;
  ranked.reserve(groups.size());
  for (auto const& group : groups) {
    auto elevation = group.weightedElevation / group.coverage;
    ranked.push_back(
        {elevation, group.minimumElevation, group.maximumElevation,
         group.coverage, group.distance, cameraPosition.y >= elevation});
  }
  return ranked;
}

bool sameElevationGroup(
    SelectedLiquidSurface const& previous,
    SelectedLiquidSurface const& candidate) {
  return candidate.minimumElevation <=
             previous.maximumElevation + liquidElevationGroupingTolerance &&
         previous.minimumElevation <=
             candidate.maximumElevation + liquidElevationGroupingTolerance;
}

bool weakerCandidate(
    SelectedLiquidSurface const& left,
    SelectedLiquidSurface const& right) {
  if (left.projectedCoverage != right.projectedCoverage) {
    return left.projectedCoverage < right.projectedCoverage;
  }
  if (left.cameraDistance != right.cameraDistance) {
    return left.cameraDistance > right.cameraDistance;
  }
  return left.minimumElevation > right.minimumElevation;
}

void retainViewerSide(
    SelectedLiquidSurface& candidate,
    SelectedLiquidSurface const& previous,
    float cameraElevation) {
  candidate.viewerAbove = previous.viewerAbove;
  if (previous.viewerAbove &&
      cameraElevation <= candidate.elevation - liquidReflectionSideHysteresis) {
    candidate.viewerAbove = false;
  } else if (!previous.viewerAbove &&
             cameraElevation >=
                 candidate.elevation + liquidReflectionSideHysteresis) {
    candidate.viewerAbove = true;
  }
}
}  // namespace

std::vector<SelectedLiquidSurface> LiquidReflectionSelectionPolicy::select(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition) {
  auto candidates = rankedVisibleLiquidSurfaces(
      triangles, viewProjection, cameraPosition);
  std::vector<bool> used(candidates.size(), false);
  std::vector<std::optional<SelectedLiquidSurface>> slots(mSelected.size());

  // Match old slots before considering rank so stable planes retain their
  // image assignment while their current geometry remains visible.
  for (std::size_t slot = 0; slot < mSelected.size(); ++slot) {
    std::optional<std::size_t> bestMatch;
    float bestDifference = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if (used[index] || !sameElevationGroup(mSelected[slot], candidates[index])) {
        continue;
      }
      auto difference =
          std::abs(mSelected[slot].elevation - candidates[index].elevation);
      if (difference < bestDifference) {
        bestDifference = difference;
        bestMatch = index;
      }
    }
    if (!bestMatch) continue;
    auto retained = candidates[*bestMatch];
    // Keep the descriptor itself immutable while this logical plane survives.
    // Coverage and distance are current ranking evidence, but changing a
    // weighted elevation or visible-subset bounds would needlessly create a
    // distinct render graph and move the plane's image projection.
    retained.elevation = mSelected[slot].elevation;
    retained.minimumElevation = mSelected[slot].minimumElevation;
    retained.maximumElevation = mSelected[slot].maximumElevation;
    retainViewerSide(retained, mSelected[slot], cameraPosition.y);
    slots[slot] = retained;
    used[*bestMatch] = true;
  }

  // Departures leave a reusable image slot. Fill those slots first, then add
  // slots up to the fixed maximum in deterministic current-rank order.
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (used[index]) continue;
    auto vacancy = std::ranges::find_if(
        slots, [](auto const& slot) { return !slot.has_value(); });
    if (vacancy != slots.end()) {
      *vacancy = candidates[index];
      used[index] = true;
    } else if (slots.size() < maximumPlanarLiquidSurfaces) {
      slots.push_back(candidates[index]);
      used[index] = true;
    }
  }

  // Once every slot is occupied, only a challenger at least 20% larger than
  // the weakest incumbent may take that incumbent's slot.
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (used[index] || slots.empty()) continue;
    auto weakest = slots.begin();
    for (auto current = slots.begin() + 1; current != slots.end(); ++current) {
      if (weakerCandidate(**current, **weakest)) weakest = current;
    }
    if (candidates[index].projectedCoverage >=
        (*weakest)->projectedCoverage *
            liquidReflectionChallengerCoverageRatio) {
      *weakest = candidates[index];
      used[index] = true;
    }
  }

  mSelected.clear();
  mSelected.reserve(slots.size());
  for (auto const& slot : slots) {
    if (slot) mSelected.push_back(*slot);
  }
  return mSelected;
}

void LiquidReflectionSelectionPolicy::reset() { mSelected.clear(); }

std::vector<SelectedLiquidSurface> selectLiquidSurfaces(
    std::span<LiquidSurfaceTriangle const> triangles,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition) {
  LiquidReflectionSelectionPolicy policy;
  return policy.select(triangles, viewProjection, cameraPosition);
}

}  // namespace bw::app
