#include "LiquidReflectionSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

std::vector<SelectedLiquidSurface> selectLiquidSurfaces(
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

  if (groups.size() > maximumPlanarLiquidSurfaces) {
    groups.resize(maximumPlanarLiquidSurfaces);
  }

  std::vector<SelectedLiquidSurface> selected;
  selected.reserve(groups.size());
  for (auto const& group : groups) {
    auto elevation = group.weightedElevation / group.coverage;
    selected.push_back(
        {elevation, group.minimumElevation, group.maximumElevation,
         group.coverage, group.distance, cameraPosition.y >= elevation});
  }
  return selected;
}

}  // namespace bw::app
