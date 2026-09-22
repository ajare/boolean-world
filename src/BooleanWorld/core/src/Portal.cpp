#include "core/Portal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "common/GameDefines.h"
#include "core/Arrangement.h"
#include "core/CoreException.h"

namespace bw::core {
namespace {
constexpr float PositionTolerance = 0.002f;
constexpr float ElevationTolerance = 0.001f;
constexpr float DirectionTolerance = 0.0001f;

float cross(wp::Vector2 const& a, wp::Vector2 const& b) {
  return a.x * b.y - a.y * b.x;
}

wp::Vector2 normalized(wp::Vector2 value) {
  auto const length = value.length();
  return length > 0.0f ? value / length : wp::Vector2{};
}

bool pointOnSegment(
    wp::Vector2 const& point, wp::Vector2 const& a,
    wp::Vector2 const& b) {
  auto const edge = b - a;
  auto const length = edge.length();
  if (length <= PositionTolerance) return false;
  auto const tangent = edge / length;
  auto const projection = (point - a).dot(tangent);
  return std::abs(cross(point - a, tangent)) <= PositionTolerance &&
         projection >= -PositionTolerance &&
         projection <= length + PositionTolerance;
}

struct Interval {
  float begin{};
  float end{};
  uint32_t wallIndex{};
};

// Weighted union-find for elevation constraints. weight[node] is the local
// surface-elevation offset from node to its parent. A constraint (a,b,delta)
// means elevation(b) - elevation(a) == delta.
class ElevationOffsetGraph {
  std::vector<uint32_t> mParent;
  std::vector<uint32_t> mSize;
  std::vector<double> mWeight;

  std::pair<uint32_t, double> rootAndWeight(uint32_t node) {
    if (mParent[node] == node) return {node, 0.0};
    auto [root, parentWeight] = rootAndWeight(mParent[node]);
    mWeight[node] += parentWeight;
    mParent[node] = root;
    return {root, mWeight[node]};
  }

public:
  explicit ElevationOffsetGraph(size_t count)
      : mParent(count), mSize(count, 1), mWeight(count, 0.0) {
    std::iota(mParent.begin(), mParent.end(), 0u);
  }

  bool constrain(uint32_t first, uint32_t second, double delta) {
    auto [firstRoot, firstWeight] = rootAndWeight(first);
    auto [secondRoot, secondWeight] = rootAndWeight(second);
    if (firstRoot == secondRoot) {
      return std::abs((secondWeight - firstWeight) - delta) <=
             ElevationTolerance;
    }

    // potential(secondRoot) - potential(firstRoot)
    auto secondToFirst = delta + firstWeight - secondWeight;
    if (mSize[firstRoot] < mSize[secondRoot]) {
      mParent[firstRoot] = secondRoot;
      mWeight[firstRoot] = -secondToFirst;
      mSize[secondRoot] += mSize[firstRoot];
    } else {
      mParent[secondRoot] = firstRoot;
      mWeight[secondRoot] = secondToFirst;
      mSize[firstRoot] += mSize[secondRoot];
    }
    return true;
  }
};

// Clips [begin,end], measured in world units along the candidate tangent, to
// one affine inequality value(x) <= limit or value(x) >= limit.
bool clipAffineInterval(
    float& begin, float& end, float origin, float slope, float limit,
    bool lessOrEqual) {
  auto satisfies = [&](float x) {
    auto const value = origin + slope * x;
    return lessOrEqual ? value <= limit + ElevationTolerance
                       : value >= limit - ElevationTolerance;
  };
  auto const beginInside = satisfies(begin);
  auto const endInside = satisfies(end);
  if (beginInside && endInside) return true;
  if (!beginInside && !endInside) {
    if (std::abs(slope) <= std::numeric_limits<float>::epsilon()) return false;
    auto const crossing = (limit - origin) / slope;
    if (crossing < begin - PositionTolerance ||
        crossing > end + PositionTolerance) {
      return false;
    }
  }
  if (std::abs(slope) <= std::numeric_limits<float>::epsilon()) return false;
  auto const crossing = std::clamp((limit - origin) / slope, begin, end);
  if (!beginInside) begin = crossing;
  if (!endInside) end = crossing;
  return end >= begin - PositionTolerance;
}

ResolvedPortalEndpoint resolveEndpoint(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    PortalEndpoint const& endpoint, float resolvedWidth) {
  ResolvedPortalEndpoint result;
  result.endpointId = endpoint.getId();
  result.authored = endpoint.getAperture();

  struct Candidate {
    uint32_t index;
    arr::ArrangementWallOrientation orientation;
  };
  std::vector<Candidate> centreWalls;
  for (uint32_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) continue;
    auto orientation = arr::OrientArrangementWall(arrangement, wall);
    if (pointOnSegment(
            result.authored.centre, orientation.v0, orientation.v1)) {
      centreWalls.push_back({wallIndex, orientation});
    }
  }

  if (centreWalls.empty()) {
    result.diagnostic = PortalResolutionDiagnostic::MissingRenderedWall;
    return result;
  }

  auto const halfWidth = resolvedWidth * 0.5f;
  // Try every rendered wall through the centre in generated order. This makes
  // a corner deterministic while still allowing a later coincident wall span
  // to succeed when the first does not cover the requested vertical range.
  for (auto const& candidate : centreWalls) {
    auto const candidateEdge = candidate.orientation.v1 - candidate.orientation.v0;
    auto const tangent = normalized(candidateEdge);
    if (tangent.lengthSq() == 0.0f) continue;
    auto const front = normalized(candidate.orientation.normal);

    std::vector<Interval> intervals;
    for (uint32_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
      auto const& wall = walls[wallIndex];
      if (!wall.visible) continue;
      auto orientation = arr::OrientArrangementWall(arrangement, wall);
      auto const wallDirection = orientation.v1 - orientation.v0;
      auto const wallLength = wallDirection.length();
      if (wallLength <= PositionTolerance) continue;
      auto const wallTangent = wallDirection / wallLength;

      if (std::abs(cross(wallTangent, tangent)) > DirectionTolerance ||
          std::abs(cross(orientation.v0 - result.authored.centre, tangent)) >
              PositionTolerance ||
          normalized(orientation.normal).dot(front) < 1.0f - DirectionTolerance) {
        continue;
      }

      auto x0 = (orientation.v0 - result.authored.centre).dot(tangent);
      auto x1 = (orientation.v1 - result.authored.centre).dot(tangent);
      auto bottom0 = orientation.bottomZ[0];
      auto bottom1 = orientation.bottomZ[1];
      auto top0 = orientation.topZ[0];
      auto top1 = orientation.topZ[1];
      if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(bottom0, bottom1);
        std::swap(top0, top1);
      }

      auto begin = std::max(x0, -halfWidth);
      auto end = std::min(x1, halfWidth);
      if (end < begin + PositionTolerance) continue;

      auto const span = x1 - x0;
      auto const bottomSlope = (bottom1 - bottom0) / span;
      auto const topSlope = (top1 - top0) / span;
      auto const bottomOrigin = bottom0 - bottomSlope * x0;
      auto const topOrigin = top0 - topSlope * x0;
      if (!clipAffineInterval(
              begin, end, bottomOrigin, bottomSlope,
              result.authored.bottom, true) ||
          !clipAffineInterval(
              begin, end, topOrigin, topSlope, result.authored.top,
              false)) {
        continue;
      }
      if (end >= begin + PositionTolerance) {
        intervals.push_back({begin, end, wallIndex});
      }
    }

    std::sort(intervals.begin(), intervals.end(), [](auto const& a, auto const& b) {
      if (a.begin != b.begin) return a.begin < b.begin;
      if (a.end != b.end) return a.end < b.end;
      return a.wallIndex < b.wallIndex;
    });

    auto coveredTo = -halfWidth;
    std::vector<uint32_t> wallIndices;
    for (auto const& interval : intervals) {
      if (interval.end < coveredTo - PositionTolerance) continue;
      if (interval.begin > coveredTo + PositionTolerance) break;
      coveredTo = std::max(coveredTo, interval.end);
      if (std::find(wallIndices.begin(), wallIndices.end(), interval.wallIndex) ==
          wallIndices.end()) {
        wallIndices.push_back(interval.wallIndex);
      }
      if (coveredTo >= halfWidth - PositionTolerance) break;
    }

    if (coveredTo < halfWidth - PositionTolerance) continue;

    result.resolved = true;
    result.diagnostic = PortalResolutionDiagnostic::None;
    result.aperture = {
        result.authored.centre, tangent, front, resolvedWidth,
        result.authored.bottom, result.authored.top, std::move(wallIndices)};
    return result;
  }

  result.diagnostic =
      PortalResolutionDiagnostic::IncompleteRenderedWallCoverage;
  return result;
}
}  // namespace

wp::Vector2 PortalRigidTransform::transformPoint(
    wp::Vector2 const& point) const {
  auto offset = point - source.centre;
  auto tangentCoordinate = offset.dot(source.tangent);
  auto frontCoordinate = offset.dot(source.front);
  return destination.centre - destination.tangent * tangentCoordinate -
         destination.front * frontCoordinate;
}

wp::Vector2 PortalRigidTransform::transformVector(
    wp::Vector2 const& vector) const {
  return -destination.tangent * vector.dot(source.tangent) -
         destination.front * vector.dot(source.front);
}

float PortalRigidTransform::transformElevation(float elevation) const {
  return destination.bottom + elevation - source.bottom;
}

float PortalRigidTransform::transformYaw(float yawDegrees) const {
  auto forward = wp::Vector2::fromAngle(yawDegrees, wp::Clockwise);
  return transformVector(forward).clockwiseAngle();
}

PortalRigidTransform BuildPortalRigidTransform(
    ResolvedPortalPair const& pair, uint32_t sourceEndpoint) {
  if (!pair.active || sourceEndpoint >= pair.endpoints.size()) {
    throw CoreException("A Portal rigid transform requires an active pair and a valid source endpoint");
  }
  return {
      pair.endpoints[sourceEndpoint].aperture,
      pair.endpoints[1u - sourceEndpoint].aperture};
}

PortalEndpoint::PortalEndpoint(uint8_t id, AuthoredAperture aperture)
    : mId(id), mAperture(aperture) {
  if (id > 1 || !AuthoredApertureIsValid(aperture)) {
    throw CoreException("Invalid Portal endpoint");
  }
}

uint8_t PortalEndpoint::getId() const { return mId; }

AuthoredAperture const& PortalEndpoint::getAperture() const {
  return mAperture;
}

void PortalEndpoint::setAperture(AuthoredAperture const& aperture) {
  if (!AuthoredApertureIsValid(aperture)) {
    throw CoreException(
        "Portal aperture centre and dimensions must be finite, with positive width and top above bottom");
  }
  mAperture = aperture;
}

PortalPair::PortalPair(
    uint32_t id, AuthoredAperture first, AuthoredAperture second)
    : mId(id),
      mEndpoints{PortalEndpoint{0, first}, PortalEndpoint{1, second}} {
}

uint32_t PortalPair::getId() const { return mId; }

PortalEndpoint const& PortalPair::getEndpoint(uint32_t index) const {
  if (index >= mEndpoints.size()) {
    throw std::out_of_range("Portal endpoint index");
  }
  return mEndpoints[index];
}

PortalEndpoint& PortalPair::endpoint(uint32_t index) {
  if (index >= mEndpoints.size()) {
    throw std::out_of_range("Portal endpoint index");
  }
  return mEndpoints[index];
}

std::string_view PortalResolutionDiagnosticText(
    PortalResolutionDiagnostic diagnostic) {
  switch (diagnostic) {
    case PortalResolutionDiagnostic::None:
      return "Active";
    case PortalResolutionDiagnostic::UnequalEndpointHeights:
      return "Inactive: endpoint heights differ";
    case PortalResolutionDiagnostic::InsufficientPlayerWidth:
      return "Inactive: aperture is narrower than the player";
    case PortalResolutionDiagnostic::InsufficientPlayerHeight:
      return "Inactive: aperture is shorter than the player";
    case PortalResolutionDiagnostic::MissingRenderedWall:
      return "Inactive: no rendered ArrangementWall at endpoint";
    case PortalResolutionDiagnostic::IncompleteRenderedWallCoverage:
      return "Inactive: rendered ArrangementWall coverage is incomplete";
    case PortalResolutionDiagnostic::OtherEndpointUnresolved:
      return "Inactive: linked endpoint did not resolve";
  }
  return "Inactive: unknown Portal resolution failure";
}

std::string_view PortalLiquidDiagnosticText(
    PortalLiquidDiagnostic diagnostic) {
  switch (diagnostic) {
    case PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint:
      return "No Hydraulic cell touches one resolved aperture";
    case PortalLiquidDiagnostic::ContradictoryElevationCycle:
      return "Contradictory accumulated Portal Liquid elevation offset";
  }
  return "Unknown Portal Liquid failure";
}

bool AuthoredApertureIsValid(AuthoredAperture const& aperture) {
  return std::isfinite(aperture.centre.x) &&
         std::isfinite(aperture.centre.y) && std::isfinite(aperture.width) &&
         std::isfinite(aperture.bottom) && std::isfinite(aperture.top) &&
         aperture.width > 0.0f && aperture.top > aperture.bottom;
}

std::vector<ResolvedPortalPair> ResolvePortalPairs(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<PortalPairSnapshot> const& pairs) {
  std::vector<ResolvedPortalPair> result;
  result.reserve(pairs.size());
  for (auto const& snapshot : pairs) {
    ResolvedPortalPair resolved;
    resolved.layerId = snapshot.layerId;
    resolved.pairId = snapshot.pair.getId();
    auto const& first = snapshot.pair.getEndpoint(0);
    auto const& second = snapshot.pair.getEndpoint(1);
    auto const firstHeight =
        first.getAperture().top - first.getAperture().bottom;
    auto const secondHeight =
        second.getAperture().top - second.getAperture().bottom;
    auto const width =
        std::min(first.getAperture().width, second.getAperture().width);
    resolved.endpoints[0] =
        resolveEndpoint(arrangement, walls, first, width);
    resolved.endpoints[1] =
        resolveEndpoint(arrangement, walls, second, width);

    // Pair-wide validation does not discard successful geometric resolution:
    // the editor still needs the generated tangent and narrowed bounds to
    // distinguish resolved geometry from the reason the pair is inactive.
    auto pairFailure = PortalResolutionDiagnostic::None;
    if (std::abs(firstHeight - secondHeight) > ElevationTolerance) {
      pairFailure = PortalResolutionDiagnostic::UnequalEndpointHeights;
    } else if (width + PositionTolerance < 2.0f * BW_PLAYER_RADIUS) {
      pairFailure = PortalResolutionDiagnostic::InsufficientPlayerWidth;
    } else if (firstHeight + ElevationTolerance < BW_PLAYER_HEIGHT) {
      pairFailure = PortalResolutionDiagnostic::InsufficientPlayerHeight;
    }
    if (pairFailure != PortalResolutionDiagnostic::None) {
      resolved.diagnostic = pairFailure;
      resolved.endpoints[0].diagnostic = pairFailure;
      resolved.endpoints[1].diagnostic = pairFailure;
    } else if (resolved.endpoints[0].resolved && resolved.endpoints[1].resolved) {
      resolved.active = true;
      resolved.diagnostic = PortalResolutionDiagnostic::None;
    } else {
      resolved.diagnostic = !resolved.endpoints[0].resolved
                                ? resolved.endpoints[0].diagnostic
                                : resolved.endpoints[1].diagnostic;
      if (resolved.endpoints[0].resolved) {
        resolved.endpoints[0].diagnostic =
            PortalResolutionDiagnostic::OtherEndpointUnresolved;
      }
      if (resolved.endpoints[1].resolved) {
        resolved.endpoints[1].diagnostic =
            PortalResolutionDiagnostic::OtherEndpointUnresolved;
      }
    }
    result.push_back(std::move(resolved));
  }
  return result;
}

PortalLiquidAdjacencyResult BuildPortalLiquidAdjacency(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<arr::HydraulicCell> const& cells,
    std::vector<ResolvedPortalPair> const& pairs) {
  PortalLiquidAdjacencyResult result;
  auto ordinaryLinks = arr::BuildHydraulicLinks(arrangement, cells);
  ElevationOffsetGraph offsets(cells.size());
  for (auto const& link : ordinaryLinks) {
    if (!link.drain) offsets.constrain(link.cell0, link.cell1, 0.0);
  }

  auto incidentCells = [&](ResolvedPortalEndpoint const& endpoint) {
    std::vector<uint32_t> found;
    for (auto wallIndex : endpoint.aperture.wallIndices) {
      if (wallIndex >= walls.size()) continue;
      auto const& wall = walls[wallIndex];
      if (wall.edge >= arrangement.edges.size()) continue;
      auto const& edge = arrangement.edges[wall.edge];
      for (uint32_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
        auto const& triangle = cells[cellIndex].triangle;
        if (triangle.face != wall.frontFace) continue;
        auto hasBoundaryEdge = false;
        for (size_t corner = 0; corner < 3; ++corner) {
          auto first = triangle.v[corner];
          auto second = triangle.v[(corner + 1) % 3];
          hasBoundaryEdge |=
              (first == edge.v[0] && second == edge.v[1]) ||
              (first == edge.v[1] && second == edge.v[0]);
        }
        if (hasBoundaryEdge) found.push_back(cellIndex);
      }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
  };

  std::vector<ResolvedPortalPair const*> orderedPairs;
  orderedPairs.reserve(pairs.size());
  for (auto const& pair : pairs) {
    if (pair.active) orderedPairs.push_back(&pair);
  }
  std::sort(
      orderedPairs.begin(), orderedPairs.end(), [](auto* left, auto* right) {
        return std::tie(left->layerId, left->pairId) <
               std::tie(right->layerId, right->pairId);
      });

  for (auto const* pair : orderedPairs) {
    auto firstCells = incidentCells(pair->endpoints[0]);
    auto secondCells = incidentCells(pair->endpoints[1]);
    if (firstCells.empty() || secondCells.empty()) {
      result.diagnostics.push_back(
          {pair->layerId, pair->pairId,
           PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint});
      continue;
    }

    auto const& first = pair->endpoints[0].aperture;
    auto const& second = pair->endpoints[1].aperture;
    auto const delta = double(second.bottom) - double(first.bottom);
    auto trial = offsets;
    auto consistent = true;
    for (auto cell0 : firstCells) {
      for (auto cell1 : secondCells) {
        consistent &= trial.constrain(cell0, cell1, delta);
      }
    }
    if (!consistent) {
      result.diagnostics.push_back(
          {pair->layerId, pair->pairId,
           PortalLiquidDiagnostic::ContradictoryElevationCycle});
      continue;
    }
    offsets = std::move(trial);

    for (auto cell0 : firstCells) {
      for (auto cell1 : secondCells) {
        result.adjacency.push_back(
            {pair->layerId,
             pair->pairId,
             cell0,
             cell1,
             cells[cell0].triangle.face,
             cells[cell1].triangle.face,
             first.bottom,
             second.bottom,
             delta,
             first.width});
      }
    }
  }
  return result;
}

}  // namespace bw::core
