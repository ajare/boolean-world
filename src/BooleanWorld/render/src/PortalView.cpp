#include "PortalView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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

float projectedArea(
    bw::core::ResolvedAperture const& aperture,
    glm::mat4 const& viewProjection) {
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
    polygon.push_back(viewProjection * glm::vec4(corner, 1.0f));
  }
  polygon = clipToFrustum(std::move(polygon));
  if (polygon.size() < 3) return 0.0f;

  float twiceArea = 0.0f;
  for (size_t index = 0; index < polygon.size(); ++index) {
    auto const& first = polygon[index];
    auto const& second = polygon[(index + 1) % polygon.size()];
    if (first.w <= 0.0f || second.w <= 0.0f) return 0.0f;
    auto a = glm::vec2(first) / first.w;
    auto b = glm::vec2(second) / second.w;
    twiceArea += a.x * b.y - b.x * a.y;
  }
  return std::abs(twiceArea) * 0.5f;
}

glm::mat4 sourceToDestinationMatrix(
    bw::core::PortalRigidTransform const& transform) {
  auto x = transform.transformVector({1.0f, 0.0f});
  auto y = transform.transformVector({0.0f, 1.0f});
  auto origin = transform.transformPoint({0.0f, 0.0f});
  auto elevationOffset = transform.transformElevation(0.0f);

  glm::mat4 result{1.0f};
  result[0] = {x.x, 0.0f, -x.y, 0.0f};
  result[1] = {0.0f, 1.0f, 0.0f, 0.0f};
  result[2] = {-y.x, 0.0f, y.y, 0.0f};
  result[3] = {origin.x, elevationOffset, -origin.y, 1.0f};
  return result;
}
}  // namespace

std::optional<SelectedPortalView> SelectPortalView(
    std::span<bw::core::ResolvedPortalPair const> pairs,
    glm::mat4 const& viewProjection,
    glm::vec3 const& cameraPosition) {
  std::vector<SelectedPortalView> candidates;
  auto cameraWorldPlane = wp::Vector2{cameraPosition.x, -cameraPosition.z};

  for (auto const& pair : pairs) {
    if (!pair.active) continue;
    for (uint32_t endpointIndex = 0;
         endpointIndex < pair.endpoints.size(); ++endpointIndex) {
      auto const& endpoint = pair.endpoints[endpointIndex];
      if (!endpoint.resolved) continue;
      auto const& aperture = endpoint.aperture;
      if (aperture.front.dot(cameraWorldPlane - aperture.centre) <= 0.0f) {
        continue;
      }
      auto coverage = projectedArea(aperture, viewProjection);
      if (!std::isfinite(coverage) || coverage <= 1e-8f) continue;
      auto centre = rendererPosition(
          aperture.centre, (aperture.bottom + aperture.top) * 0.5f);
      auto distance = glm::distance(cameraPosition, centre);
      if (!std::isfinite(distance)) continue;
      candidates.push_back({{pair.layerId, pair.pairId, endpoint.endpointId}, &pair, endpointIndex, coverage, distance});
    }
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
  if (!selected.pair || !selected.pair->active || width == 0 || height == 0) {
    throw std::invalid_argument(
        "A Portal view requires an active selection and non-zero dimensions");
  }

  auto rigid = bw::core::BuildPortalRigidTransform(
      *selected.pair, selected.sourceEndpoint);
  auto sourceToDestination = sourceToDestinationMatrix(rigid);
  auto destinationView = observingView * glm::inverse(sourceToDestination);
  auto const& destination = rigid.destination;
  auto destinationNormal = glm::vec3{
      destination.front.x, 0.0f, -destination.front.y};
  auto destinationCentre = rendererPosition(
      destination.centre, destination.bottom);
  auto clipPlane = glm::vec4{
      destinationNormal,
      -glm::dot(destinationNormal, destinationCentre)};
  auto clipped = mpp::buildObliquelyClippedVirtualCamera(
      destinationView, observingProjection, clipPlane);

  BuiltPortalView result;
  result.sourceToDestination = sourceToDestination;
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
  result.auxiliary.seamBias = mpp::AuxiliaryViewClipSeamBias;
  result.auxiliary.reverseWinding = false;
  return result;
}
