#include "TriplanarWallRenderData.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "WallNormalMapRenderData.h"

using namespace std;

namespace {
wp::Vector2 const projectionFallback{1.0f, 0.0f};
constexpr double cancellationEpsilonSquared = 1e-12;

wp::Vector2 normalizedOrFallback(double x, double y) {
  auto lengthSquared = x * x + y * y;
  if (lengthSquared <= cancellationEpsilonSquared) return projectionFallback;
  auto inverseLength = float(1.0 / sqrt(lengthSquared));
  return {float(x) * inverseLength, float(y) * inverseLength};
}

bool samePosition(wp::Vector2 const& left, wp::Vector2 const& right) {
  auto delta = left - right;
  return delta.dot(delta) <= 1e-10f;
}

array<optional<uint32_t>, 2> arrangementVerticesFor(
    bw::core::arr::ArrangementResult const& arrangement,
    bw::core::arr::ArrangementWall const& wall,
    bw::core::arr::ArrangementWallOrientation const& orientation) {
  auto const& edge = arrangement.edges[wall.edge];
  auto fixedPosition = [&](uint32_t vertex) {
    auto const& fixed = arrangement.vertices[vertex];
    return wp::Vector2{bw::core::arr::ToWorldCoordinate(fixed.x),
                       bw::core::arr::ToWorldCoordinate(fixed.y)};
  };

  vector<pair<wp::Vector2, uint32_t>> candidates;
  if (wall.sourceEdgeParameter[0] == 0.0f) {
    candidates.emplace_back(fixedPosition(edge.v[0]), edge.v[0]);
  }
  if (wall.sourceEdgeParameter[1] == 1.0f) {
    candidates.emplace_back(fixedPosition(edge.v[1]), edge.v[1]);
  }

  array<optional<uint32_t>, 2> result;
  for (size_t endpoint = 0; endpoint < result.size(); ++endpoint) {
    auto const& position = endpoint == 0 ? orientation.v0 : orientation.v1;
    for (auto const& [candidatePosition, candidateVertex] : candidates) {
      if (samePosition(position, candidatePosition)) {
        result[endpoint] = candidateVertex;
        break;
      }
    }
  }
  return result;
}

struct IncidentWall {
  size_t wall{};
  size_t endpoint{};
  float lower{};
  float upper{};
  wp::Vector2 normal{};
};

struct ClipVertex {
  wp::Vector2 position;
  float elevation{};
  float endpointParameter{};
};

vector<ClipVertex> clipAtElevation(
    vector<ClipVertex> const& input, float elevation, bool keepAbove) {
  vector<ClipVertex> output;
  if (input.empty()) return output;
  auto inside = [&](ClipVertex const& vertex) {
    return keepAbove ? vertex.elevation >= elevation
                     : vertex.elevation <= elevation;
  };
  auto intersection = [&](ClipVertex const& from, ClipVertex const& to) {
    auto denominator = to.elevation - from.elevation;
    auto amount = denominator == 0.0f
                      ? 0.0f
                      : (elevation - from.elevation) / denominator;
    return ClipVertex{
        from.position + (to.position - from.position) * amount, elevation,
        from.endpointParameter +
            (to.endpointParameter - from.endpointParameter) * amount};
  };

  auto previous = input.back();
  auto previousInside = inside(previous);
  for (auto const& current : input) {
    auto currentInside = inside(current);
    if (currentInside != previousInside) {
      output.push_back(intersection(previous, current));
    }
    if (currentInside) output.push_back(current);
    previous = current;
    previousInside = currentInside;
  }

  vector<ClipVertex> cleaned;
  for (auto const& vertex : output) {
    if (!cleaned.empty() &&
        samePosition(cleaned.back().position, vertex.position) &&
        cleaned.back().elevation == vertex.elevation) {
      continue;
    }
    cleaned.push_back(vertex);
  }
  if (cleaned.size() > 1 &&
      samePosition(cleaned.front().position, cleaned.back().position) &&
      cleaned.front().elevation == cleaned.back().elevation) {
    cleaned.pop_back();
  }
  return cleaned;
}

wp::Vector2 projectionNormalAt(
    TriplanarWallProjectionEndpoint const& endpoint, float elevation,
    wp::Vector2 const& geometricNormal) {
  for (auto const& span : endpoint.spans) {
    if (elevation >= span.lowerElevation &&
        elevation < span.upperElevation) {
      return span.projectionNormal;
    }
  }
  if (!endpoint.spans.empty() &&
      elevation == endpoint.spans.back().upperElevation) {
    return endpoint.spans.back().projectionNormal;
  }
  return geometricNormal;
}

wp::Vector2 interpolateProjectionNormal(
    wp::Vector2 const& first, wp::Vector2 const& second, float amount) {
  if (amount <= 0.0f) return first;
  if (amount >= 1.0f) return second;
  auto blended = first * (1.0f - amount) + second * amount;
  auto lengthSquared = blended.dot(blended);
  if (lengthSquared <= float(cancellationEpsilonSquared)) {
    return projectionFallback;
  }
  return blended / sqrt(lengthSquared);
}
}  // namespace

TriplanarWallProjectionData BuildTriplanarWallProjectionData(
    bw::core::arr::ArrangementResult const& arrangement,
    vector<bw::core::arr::ArrangementWall> const& walls) {
  TriplanarWallProjectionData result(walls.size());
  using GroupKey = tuple<uint32_t, string>;
  map<GroupKey, vector<IncidentWall>> groups;

  for (size_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (wall.paletteIndex >= arrangement.palette.size()) continue;
    auto const& material = arrangement.palette[wall.paletteIndex].wallMaterial;
    if (material.kind != bw::core::SurfaceMaterialKind::Triplanar) continue;

    auto& wallResult = result[wallIndex];
    wallResult.usesTriplanar = true;
    auto orientation = bw::core::arr::OrientArrangementWall(arrangement, wall);
    auto arrangementVertices = arrangementVerticesFor(
        arrangement, wall, orientation);
    for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
      wallResult.endpoints[endpoint].arrangementVertex =
          arrangementVertices[endpoint];
      if (!arrangementVertices[endpoint] ||
          orientation.topZ[endpoint] <= orientation.bottomZ[endpoint]) {
        continue;
      }
      groups[{*arrangementVertices[endpoint], material.reference}].push_back(
          {wallIndex, endpoint, orientation.bottomZ[endpoint],
           orientation.topZ[endpoint], orientation.normal});
    }
  }

  for (auto const& [key, incidents] : groups) {
    (void)key;
    vector<float> boundaries;
    boundaries.reserve(incidents.size() * 2);
    for (auto const& incident : incidents) {
      boundaries.push_back(incident.lower);
      boundaries.push_back(incident.upper);
    }
    sort(boundaries.begin(), boundaries.end());
    boundaries.erase(unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());

    for (size_t boundary = 0; boundary + 1 < boundaries.size(); ++boundary) {
      auto lower = boundaries[boundary];
      auto upper = boundaries[boundary + 1];
      if (upper <= lower) continue;
      auto sample = lower + (upper - lower) * 0.5f;
      double sumX = 0.0;
      double sumY = 0.0;
      vector<IncidentWall const*> active;
      for (auto const& incident : incidents) {
        if (sample >= incident.lower && sample < incident.upper) {
          active.push_back(&incident);
          sumX += incident.normal.x;
          sumY += incident.normal.y;
        }
      }
      if (active.empty()) continue;
      auto projectionNormal = normalizedOrFallback(sumX, sumY);
      for (auto const* incident : active) {
        result[incident->wall]
            .endpoints[incident->endpoint]
            .spans.push_back({lower, upper, projectionNormal});
      }
    }
  }

  return result;
}

vector<TriplanarWallRenderTriangle> BuildTriplanarWallRenderTriangles(
    bw::core::arr::ArrangementResult const& arrangement,
    bw::core::arr::ArrangementWall const& wall,
    TriplanarWallProjection const& projection) {
  vector<TriplanarWallRenderTriangle> result;
  auto orientation = bw::core::arr::OrientArrangementWall(arrangement, wall);
  auto surface = bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
  if (surface.vertexCount < 3) return result;

  vector<ClipVertex> source;
  source.reserve(surface.vertexCount);
  float minimumElevation = surface.vertices[0].elevation;
  float maximumElevation = minimumElevation;
  for (uint8_t index = 0; index < surface.vertexCount; ++index) {
    auto const& vertex = surface.vertices[index];
    source.push_back(
        {vertex.position, vertex.elevation, float(vertex.endpoint)});
    minimumElevation = min(minimumElevation, vertex.elevation);
    maximumElevation = max(maximumElevation, vertex.elevation);
  }

  vector<float> boundaries{minimumElevation, maximumElevation};
  for (auto const& endpoint : projection.endpoints) {
    for (size_t span = 0; span + 1 < endpoint.spans.size(); ++span) {
      auto elevation = endpoint.spans[span].upperElevation;
      if (elevation > minimumElevation && elevation < maximumElevation) {
        boundaries.push_back(elevation);
      }
    }
  }
  sort(boundaries.begin(), boundaries.end());
  boundaries.erase(unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());

  auto physicalUv = CalculateWallPhysicalUv(orientation, wall);
  auto repeat = WallImageRepeat(wall);
  auto wallLength = orientation.v0.distanceTo(orientation.v1);
  auto verticalScale =
      repeat > 0.0f && wallLength > 0.0f ? repeat / wallLength : 0.0f;
  for (size_t band = 0; band + 1 < boundaries.size(); ++band) {
    auto lower = boundaries[band];
    auto upper = boundaries[band + 1];
    if (upper <= lower) continue;
    auto polygon = clipAtElevation(source, lower, true);
    polygon = clipAtElevation(polygon, upper, false);
    if (polygon.size() < 3) continue;

    auto sample = lower + (upper - lower) * 0.5f;
    auto firstNormal = projectionNormalAt(
        projection.endpoints[0], sample, orientation.normal);
    auto secondNormal = projectionNormalAt(
        projection.endpoints[1], sample, orientation.normal);
    auto convert = [&](ClipVertex const& vertex) {
      auto amount = clamp(vertex.endpointParameter, 0.0f, 1.0f);
      auto projectionNormal =
          interpolateProjectionNormal(firstNormal, secondNormal, amount);
      auto u = repeat > 0.0f
                   ? physicalUv.u0 + (physicalUv.u1 - physicalUv.u0) * amount
                   : amount;
      auto v = repeat > 0.0f
                   ? (vertex.elevation - wall.minZ) * verticalScale
                   : (maximumElevation > minimumElevation
                          ? (vertex.elevation - minimumElevation) /
                                (maximumElevation - minimumElevation)
                          : 0.0f);
      return TriplanarWallRenderVertex{
          vertex.position, vertex.elevation, projectionNormal, u, v};
    };

    for (size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
      result.push_back(
          {{{convert(polygon[0]), convert(polygon[corner]),
             convert(polygon[corner + 1])}}});
    }
  }
  return result;
}
