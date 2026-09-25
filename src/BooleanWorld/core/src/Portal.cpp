#include "core/Portal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
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

wp::Vector2 PortalMapping::transformPoint(
    wp::Vector2 const& point) const {
  auto offset = point - source.centre;
  auto tangentCoordinate = offset.dot(source.tangent);
  auto frontCoordinate = offset.dot(source.front);
  auto tangentSign = reversesHandedness() ? 1.0f : -1.0f;
  return destination.centre +
         destination.tangent * (tangentSign * tangentCoordinate) -
         destination.front * frontCoordinate;
}

wp::Vector2 PortalMapping::transformVector(
    wp::Vector2 const& vector) const {
  auto tangentSign = reversesHandedness() ? 1.0f : -1.0f;
  return destination.tangent * (tangentSign * vector.dot(source.tangent)) -
         destination.front * vector.dot(source.front);
}

float PortalMapping::transformElevation(float elevation) const {
  if (reversesHandedness()) return elevation;
  return destination.bottom + elevation - source.bottom;
}

float PortalMapping::transformYaw(float yawDegrees) const {
  auto forward = wp::Vector2::fromAngle(yawDegrees, wp::Clockwise);
  return transformVector(forward).clockwiseAngle();
}

bool PortalMapping::reversesHandedness() const {
  return kind == PortalMappingKind::Reflection;
}

double PortalMapping::elevationOffset() const {
  return reversesHandedness() ? 0.0 :
      double(destination.bottom) - double(source.bottom);
}

std::array<float, 16> PortalMapping::rendererMatrix() const {
  auto x = transformVector({1.0f, 0.0f});
  auto y = transformVector({0.0f, 1.0f});
  auto origin = transformPoint({0.0f, 0.0f});
  return {x.x, 0.0f, -x.y, 0.0f,
          0.0f, 1.0f, 0.0f, 0.0f,
          -y.x, 0.0f, y.y, 0.0f,
          origin.x, transformElevation(0.0f), -origin.y, 1.0f};
}

PortalMapping BuildPortalReflection(ResolvedAperture const& aperture) {
  return {aperture, aperture, PortalMappingKind::Reflection};
}

uint32_t NextPortalEndpointId(
    std::span<uint32_t const> order, uint32_t sourceId) {
  if (order.empty()) {
    throw CoreException("A generated Portal cycle cannot be empty");
  }
  auto found = std::find(order.begin(), order.end(), sourceId);
  if (found == order.end()) {
    throw CoreException("Portal source endpoint ID is not in the loop");
  }
  if (std::find(std::next(found), order.end(), sourceId) != order.end()) {
    throw CoreException("Duplicate Portal source endpoint ID in traversal order");
  }
  return order[(static_cast<size_t>(found - order.begin()) + 1) % order.size()];
}

ResolvedPortalEndpoint const* FindPortalEndpoint(
    ResolvedPortalLoop const& portalLoop, uint32_t endpointId) {
  auto found = std::ranges::find(
      portalLoop.endpoints, endpointId, &ResolvedPortalEndpoint::endpointId);
  return found == portalLoop.endpoints.end() ? nullptr : &*found;
}

ResolvedPortalEndpoint const* NextPortalEndpoint(
    ResolvedPortalLoop const& portalLoop, uint32_t sourceEndpointId) {
  if (!FindPortalEndpoint(portalLoop, sourceEndpointId)) return nullptr;
  return FindPortalEndpoint(
      portalLoop, NextPortalEndpointId(portalLoop.traversalOrder, sourceEndpointId));
}

PortalMapping BuildPortalMapping(
    ResolvedPortalLoop const& portalLoop, uint32_t sourceEndpointId) {
  auto const* source = FindPortalEndpoint(portalLoop, sourceEndpointId);
  auto const* destination = NextPortalEndpoint(portalLoop, sourceEndpointId);
  if (!portalLoop.active || !source || !destination) {
    throw CoreException("A Portal mapping requires an active loop and a valid source endpoint ID");
  }
  if (portalLoop.endpoints.size() == 1) return BuildPortalReflection(source->aperture);
  return {source->aperture, destination->aperture};
}

Portal::Portal(uint32_t id, std::string name, AuthoredAperture aperture,
               uint32_t targetId)
    : mId(id), mName(std::move(name)), mAperture(aperture), mTargetId(targetId) {
  if (id == ~0u || targetId == ~0u || !AuthoredApertureIsValid(aperture) ||
      mName.empty() || mName.find_first_not_of(" \t\r\n\f\v") != 0 ||
      mName.find_last_not_of(" \t\r\n\f\v") != mName.size() - 1) {
    throw CoreException("Invalid Portal ID, name, aperture, or target");
  }
}

PortalEndpoint::PortalEndpoint(uint32_t id, AuthoredAperture aperture)
    : mId(id), mAperture(aperture) {
  if (!AuthoredApertureIsValid(aperture)) {
    throw CoreException("Invalid Portal endpoint");
  }
}

uint32_t PortalEndpoint::getId() const { return mId; }

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

PortalLoop::PortalLoop(
    uint32_t id, AuthoredAperture first, AuthoredAperture second)
    : PortalLoop(
          id, 2,
          {PortalEndpoint{0, first}, PortalEndpoint{1, second}},
          {0, 1}) {}

PortalLoop::PortalLoop(
    uint32_t id, uint32_t nextEndpointId,
    std::vector<PortalEndpoint> endpoints,
    std::vector<uint32_t> traversalOrder)
    : mId(id),
      mNextEndpointId(nextEndpointId),
      mEndpoints(std::move(endpoints)),
      mTraversalOrder(std::move(traversalOrder)) {
  if (mEndpoints.size() < 2 || mTraversalOrder.size() != mEndpoints.size()) {
    throw CoreException("A Portal loop requires at least two endpoints and one traversal entry per endpoint");
  }
  std::set<uint32_t> endpointIds;
  for (auto const& endpoint : mEndpoints) {
    if (!endpointIds.insert(endpoint.getId()).second ||
        endpoint.getId() >= mNextEndpointId) {
      throw CoreException("A Portal loop contains duplicate endpoint IDs or an identity-reusing allocator state");
    }
  }
  std::set<uint32_t> orderedIds;
  for (auto endpointId : mTraversalOrder) {
    if (!endpointIds.contains(endpointId) || !orderedIds.insert(endpointId).second) {
      throw CoreException("Portal traversal order is invalid");
    }
  }
}

uint32_t PortalLoop::getId() const { return mId; }

uint32_t PortalLoop::getNextEndpointAllocator() const {
  return mNextEndpointId;
}

std::vector<PortalEndpoint> const& PortalLoop::getEndpoints() const {
  return mEndpoints;
}

PortalEndpoint const* PortalLoop::findEndpoint(uint32_t endpointId) const {
  auto found = std::ranges::find(mEndpoints, endpointId, &PortalEndpoint::getId);
  return found == mEndpoints.end() ? nullptr : &*found;
}

PortalEndpoint* PortalLoop::findEndpointMutable(uint32_t endpointId) {
  auto found = std::ranges::find(mEndpoints, endpointId, &PortalEndpoint::getId);
  return found == mEndpoints.end() ? nullptr : &*found;
}

std::span<uint32_t const> PortalLoop::getTraversalOrder() const {
  return mTraversalOrder;
}

uint32_t PortalLoop::getNextEndpointId(uint32_t endpointId) const {
  return NextPortalEndpointId(mTraversalOrder, endpointId);
}

uint32_t PortalLoop::addEndpointAfter(
    uint32_t afterEndpointId, AuthoredAperture const& aperture) {
  auto after = std::ranges::find(mTraversalOrder, afterEndpointId);
  if (after == mTraversalOrder.end()) {
    throw CoreException("Portal insertion endpoint ID is not in the loop");
  }
  if (mNextEndpointId == std::numeric_limits<uint32_t>::max()) {
    throw CoreException("Portal endpoint ID space exhausted");
  }
  auto const id = mNextEndpointId++;
  mEndpoints.emplace_back(id, aperture);
  mTraversalOrder.insert(std::next(after), id);
  return id;
}

void PortalLoop::removeEndpoint(uint32_t endpointId) {
  if (mEndpoints.size() <= 2) {
    throw CoreException("A Portal loop cannot contain fewer than two endpoints");
  }
  auto endpoint = std::ranges::find(mEndpoints, endpointId, &PortalEndpoint::getId);
  auto order = std::ranges::find(mTraversalOrder, endpointId);
  if (endpoint == mEndpoints.end() || order == mTraversalOrder.end()) {
    throw CoreException("Portal endpoint ID is not in the loop");
  }
  mEndpoints.erase(endpoint);
  mTraversalOrder.erase(order);
}

bool PortalLoop::moveEndpointEarlier(uint32_t endpointId) {
  auto found = std::ranges::find(mTraversalOrder, endpointId);
  if (found == mTraversalOrder.end()) {
    throw CoreException("Portal endpoint ID is not in the loop");
  }
  if (found == mTraversalOrder.begin()) return false;
  std::iter_swap(found, std::prev(found));
  return true;
}

bool PortalLoop::moveEndpointLater(uint32_t endpointId) {
  auto found = std::ranges::find(mTraversalOrder, endpointId);
  if (found == mTraversalOrder.end()) {
    throw CoreException("Portal endpoint ID is not in the loop");
  }
  if (std::next(found) == mTraversalOrder.end()) return false;
  std::iter_swap(found, std::next(found));
  return true;
}

std::string_view PortalTargetGraphDiagnosticText(
    PortalTargetGraphDiagnostic diagnostic) {
  switch (diagnostic) {
    case PortalTargetGraphDiagnostic::None:
      return "Target graph: complete cycle";
    case PortalTargetGraphDiagnostic::MissingIncomingReference:
      return "Target graph inactive: no Portal targets this Portal";
    case PortalTargetGraphDiagnostic::MultipleIncomingReferences:
      return "Target graph inactive: multiple Portals target this Portal";
    case PortalTargetGraphDiagnostic::OtherPortalInvalid:
      return "Target graph inactive: another Portal in this component has invalid incoming references";
  }
  return "Target graph inactive: unknown failure";
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

std::optional<wp::Vector2> FindNearestLegalPortalCentre(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    AuthoredAperture const& aperture, float resolvedWidth,
    wp::Vector2 const& target, float maxDistance) {
  if (!AuthoredApertureIsValid(aperture) ||
      !std::isfinite(resolvedWidth) || resolvedWidth <= 0.0f ||
      !std::isfinite(target.x) || !std::isfinite(target.y) ||
      !std::isfinite(maxDistance) || maxDistance < 0.0f) {
    return std::nullopt;
  }

  auto const halfWidth = resolvedWidth * 0.5f;
  auto const maxDistanceSq = maxDistance * maxDistance;
  std::optional<wp::Vector2> nearest;
  auto nearestDistanceSq = std::numeric_limits<float>::infinity();

  // Each rendered wall acts as a representative for its collinear,
  // consistently oriented coverage. Reconsidering the same line is harmless
  // and preserves generated wall order as the deterministic tie-break.
  for (auto const& candidateWall : walls) {
    if (!candidateWall.visible) continue;
    auto const candidate =
        arr::OrientArrangementWall(arrangement, candidateWall);
    auto const candidateEdge = candidate.v1 - candidate.v0;
    auto const tangent = normalized(candidateEdge);
    if (tangent.lengthSq() == 0.0f) continue;
    if (std::abs(cross(target - candidate.v0, tangent)) >
        maxDistance + PositionTolerance) {
      continue;
    }
    auto const front = normalized(candidate.normal);

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
          std::abs(cross(orientation.v0 - candidate.v0, tangent)) >
              PositionTolerance ||
          normalized(orientation.normal).dot(front) <
              1.0f - DirectionTolerance) {
        continue;
      }

      auto x0 = (orientation.v0 - candidate.v0).dot(tangent);
      auto x1 = (orientation.v1 - candidate.v0).dot(tangent);
      auto bottom0 = orientation.bottomZ[0];
      auto bottom1 = orientation.bottomZ[1];
      auto top0 = orientation.topZ[0];
      auto top1 = orientation.topZ[1];
      if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(bottom0, bottom1);
        std::swap(top0, top1);
      }

      auto begin = x0;
      auto end = x1;
      auto const span = x1 - x0;
      if (span <= PositionTolerance) continue;
      auto const bottomSlope = (bottom1 - bottom0) / span;
      auto const topSlope = (top1 - top0) / span;
      auto const bottomOrigin = bottom0 - bottomSlope * x0;
      auto const topOrigin = top0 - topSlope * x0;
      if (clipAffineInterval(
              begin, end, bottomOrigin, bottomSlope, aperture.bottom, true) &&
          clipAffineInterval(
              begin, end, topOrigin, topSlope, aperture.top, false) &&
          end >= begin + PositionTolerance) {
        intervals.push_back({begin, end, wallIndex});
      }
    }
    if (intervals.empty()) continue;

    std::sort(intervals.begin(), intervals.end(), [](auto const& a, auto const& b) {
      if (a.begin != b.begin) return a.begin < b.begin;
      if (a.end != b.end) return a.end < b.end;
      return a.wallIndex < b.wallIndex;
    });

    auto considerCoverage = [&](float begin, float end) {
      if (end - begin < resolvedWidth - PositionTolerance) return;
      auto legalBegin = begin + halfWidth;
      auto legalEnd = end - halfWidth;
      if (legalEnd < legalBegin) {
        auto const midpoint = (legalBegin + legalEnd) * 0.5f;
        legalBegin = midpoint;
        legalEnd = midpoint;
      }
      auto const targetX = (target - candidate.v0).dot(tangent);
      auto const snappedX = std::clamp(targetX, legalBegin, legalEnd);
      auto const snapped = candidate.v0 + tangent * snappedX;
      auto const distanceSq = snapped.distanceToSq(target);
      if (distanceSq <= maxDistanceSq + PositionTolerance &&
          distanceSq < nearestDistanceSq) {
        nearest = snapped;
        nearestDistanceSq = distanceSq;
      }
    };

    auto coverageBegin = intervals.front().begin;
    auto coverageEnd = intervals.front().end;
    for (size_t index = 1; index < intervals.size(); ++index) {
      auto const& interval = intervals[index];
      if (interval.begin <= coverageEnd + PositionTolerance) {
        coverageEnd = std::max(coverageEnd, interval.end);
      } else {
        considerCoverage(coverageBegin, coverageEnd);
        coverageBegin = interval.begin;
        coverageEnd = interval.end;
      }
    }
    considerCoverage(coverageBegin, coverageEnd);
  }

  if (!nearest) return std::nullopt;
  auto snappedAperture = aperture;
  snappedAperture.centre = *nearest;
  auto resolved = resolveEndpoint(
      arrangement, walls, PortalEndpoint{0, snappedAperture}, resolvedWidth);
  return resolved.resolved ? nearest : std::nullopt;
}

std::vector<ResolvedPortalLoop> ResolvePortalLoops(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<PortalLoopSnapshot> const& loops) {
  std::vector<ResolvedPortalLoop> result;
  result.reserve(loops.size());
  struct Input {
    uint32_t layerId, loopId;
    std::vector<uint32_t> order;
    std::vector<PortalEndpoint> endpoints;
    std::map<uint32_t, PortalTargetGraphDiagnostic> graphDiagnostics;
  };
  std::vector<Input> inputs;
  std::map<uint32_t, std::map<uint32_t, Portal const*>> named;
  for (auto const& snapshot : loops) {
    if (snapshot.portal) {
      named[snapshot.layerId].emplace(snapshot.portal->getId(), &*snapshot.portal);
    } else {
      auto order = snapshot.loop.getTraversalOrder();
      inputs.push_back({snapshot.layerId, snapshot.loop.getId(),
          {order.begin(), order.end()}, snapshot.loop.getEndpoints(), {}});
    }
  }

  // Partition each Layer's functional target graph into weakly connected
  // components. Invalid tails and branches stay present as one generated,
  // inactive component instead of allowing an embedded cycle to activate.
  for (auto const& [layerId, portals] : named) {
    std::map<uint32_t, std::vector<uint32_t>> neighbours;
    std::map<uint32_t, uint32_t> incoming;
    for (auto const& [id, portal] : portals) {
      neighbours[id];
      incoming[id] = 0;
    }
    for (auto const& [id, portal] : portals) {
      auto const target = portal->getTargetId();
      if (!portals.contains(target)) continue;
      neighbours[id].push_back(target);
      neighbours[target].push_back(id);
      ++incoming[target];
    }
    for (auto& [id, adjacent] : neighbours) {
      std::ranges::sort(adjacent);
      adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
    }

    std::set<uint32_t> visited;
    for (auto const& [start, unused] : portals) {
      if (visited.contains(start)) continue;
      std::vector<uint32_t> pending{start};
      std::vector<uint32_t> component;
      visited.insert(start);
      while (!pending.empty()) {
        auto const id = pending.back();
        pending.pop_back();
        component.push_back(id);
        for (auto adjacent : neighbours[id]) {
          if (visited.insert(adjacent).second) pending.push_back(adjacent);
        }
      }
      std::ranges::sort(component);

      Input input{layerId, IndependentPortalLoopId, component, {}, {}};
      auto graphValid = true;
      for (auto id : component) {
        auto diagnostic = PortalTargetGraphDiagnostic::None;
        if (incoming[id] == 0) {
          diagnostic = PortalTargetGraphDiagnostic::MissingIncomingReference;
        } else if (incoming[id] > 1) {
          diagnostic = PortalTargetGraphDiagnostic::MultipleIncomingReferences;
        }
        input.graphDiagnostics[id] = diagnostic;
        graphValid &= diagnostic == PortalTargetGraphDiagnostic::None;
        input.endpoints.emplace_back(id, portals.at(id)->getAperture());
      }

      // A valid component's canonical route starts at its smallest ID and
      // follows authored targets. Invalid traversal order is the sorted member
      // list, used only for deterministic identity and diagnostics.
      if (graphValid) {
        input.order.clear();
        auto id = component.front();
        do {
          input.order.push_back(id);
          id = portals.at(id)->getTargetId();
        } while (id != component.front() && input.order.size() <= component.size());
        graphValid = id == component.front() &&
                     input.order.size() == component.size();
      }
      if (!graphValid) {
        input.order = component;
        for (auto id : component) {
          if (input.graphDiagnostics[id] == PortalTargetGraphDiagnostic::None)
            input.graphDiagnostics[id] = PortalTargetGraphDiagnostic::OtherPortalInvalid;
        }
      }
      inputs.push_back(std::move(input));
    }
  }
  std::ranges::sort(inputs, [](auto const& a, auto const& b) {
    return std::tie(a.layerId, a.loopId, a.order.front()) <
           std::tie(b.layerId, b.loopId, b.order.front());
  });
  for (auto const& input : inputs) {
    ResolvedPortalLoop resolved;
    resolved.layerId = input.layerId;
    resolved.loopId = input.loopId;
    resolved.traversalOrder = input.order;
    auto const& authoredEndpoints = input.endpoints;
    auto const firstHeight = authoredEndpoints.front().getAperture().top -
                             authoredEndpoints.front().getAperture().bottom;
    auto width = std::numeric_limits<float>::infinity();
    auto equalHeights = true;
    for (auto const& endpoint : authoredEndpoints) {
      auto const& aperture = endpoint.getAperture();
      width = std::min(width, aperture.width);
      equalHeights &= std::abs(
                          (aperture.top - aperture.bottom) - firstHeight) <=
                      ElevationTolerance;
    }
    resolved.endpoints.clear();
    resolved.endpoints.reserve(authoredEndpoints.size());
    std::ranges::transform(
        authoredEndpoints, std::back_inserter(resolved.endpoints),
        [&](auto const& endpoint) {
          auto result = resolveEndpoint(arrangement, walls, endpoint, width);
          if (auto found = input.graphDiagnostics.find(endpoint.getId());
              found != input.graphDiagnostics.end()) {
            result.targetGraphDiagnostic = found->second;
          }
          return result;
        });
    for (auto const& endpoint : resolved.endpoints) {
      if (endpoint.targetGraphDiagnostic != PortalTargetGraphDiagnostic::None) {
        resolved.targetGraphDiagnostic = endpoint.targetGraphDiagnostic;
        break;
      }
    }

    // Loop-wide validation does not discard successful geometric resolution:
    // the editor still needs the generated tangent and narrowed bounds to
    // distinguish aperture failures from target-graph failures.
    auto loopFailure = PortalResolutionDiagnostic::None;
    if (!equalHeights) {
      loopFailure = PortalResolutionDiagnostic::UnequalEndpointHeights;
    } else if (width + PositionTolerance < 2.0f * BW_PLAYER_RADIUS) {
      loopFailure = PortalResolutionDiagnostic::InsufficientPlayerWidth;
    } else if (firstHeight + ElevationTolerance < BW_PLAYER_HEIGHT) {
      loopFailure = PortalResolutionDiagnostic::InsufficientPlayerHeight;
    }
    if (loopFailure != PortalResolutionDiagnostic::None) {
      resolved.diagnostic = loopFailure;
      for (auto& endpoint : resolved.endpoints) endpoint.diagnostic = loopFailure;
    } else if (!std::ranges::all_of(
                   resolved.endpoints, &ResolvedPortalEndpoint::resolved)) {
      for (auto endpointId : resolved.traversalOrder) {
        auto const* endpoint = FindPortalEndpoint(resolved, endpointId);
        if (endpoint && !endpoint->resolved) {
          resolved.diagnostic = endpoint->diagnostic;
          break;
        }
      }
      for (auto& endpoint : resolved.endpoints) {
        if (endpoint.resolved)
          endpoint.diagnostic = PortalResolutionDiagnostic::OtherEndpointUnresolved;
      }
    }
    resolved.active =
        resolved.targetGraphDiagnostic == PortalTargetGraphDiagnostic::None &&
        resolved.diagnostic == PortalResolutionDiagnostic::None &&
        std::ranges::all_of(resolved.endpoints, &ResolvedPortalEndpoint::resolved);
    result.push_back(std::move(resolved));
  }
  return result;
}

PortalLiquidAdjacencyResult BuildPortalLiquidAdjacency(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<arr::HydraulicCell> const& cells,
    std::vector<ResolvedPortalLoop> const& loops) {
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

  std::vector<ResolvedPortalLoop const*> orderedLoops;
  orderedLoops.reserve(loops.size());
  for (auto const& portalLoop : loops) {
    if (portalLoop.active && portalLoop.endpoints.size() > 1)
      orderedLoops.push_back(&portalLoop);
  }
  std::sort(
      orderedLoops.begin(), orderedLoops.end(), [](auto* left, auto* right) {
        return std::tie(left->layerId, left->loopId, left->traversalOrder.front()) <
               std::tie(right->layerId, right->loopId, right->traversalOrder.front());
      });

  for (auto const* portalLoop : orderedLoops) {
    // Stage the complete loop. Neither constraints nor hops escape a failed
    // trial, including failures discovered only at a later endpoint.
    auto trial = offsets;
    std::vector<PortalLiquidAdjacency> hops;
    auto missingCells = false;
    auto consistent = true;
    for (auto const sourceEndpointId : portalLoop->traversalOrder) {
      auto const destinationEndpointId =
          NextPortalEndpointId(portalLoop->traversalOrder, sourceEndpointId);
      auto const* sourceEndpoint =
          FindPortalEndpoint(*portalLoop, sourceEndpointId);
      auto const* destinationEndpoint =
          FindPortalEndpoint(*portalLoop, destinationEndpointId);
      if (!sourceEndpoint || !destinationEndpoint) {
        missingCells = true;
        break;
      }
      auto firstCells = incidentCells(*sourceEndpoint);
      auto secondCells = incidentCells(*destinationEndpoint);
      if (firstCells.empty() || secondCells.empty()) {
        missingCells = true;
        break;
      }

      auto const& first = sourceEndpoint->aperture;
      auto const& second = destinationEndpoint->aperture;
      // Only ordinary directed loops participate in hydraulics.
      auto const delta = BuildPortalMapping(
          *portalLoop, sourceEndpointId).elevationOffset();
      for (auto cell0 : firstCells) {
        for (auto cell1 : secondCells) {
          consistent &= trial.constrain(cell0, cell1, delta);
        }
      }
      for (auto cell0 : firstCells) {
        for (auto cell1 : secondCells) {
          hops.push_back(
              {portalLoop->layerId,
               portalLoop->loopId,
               sourceEndpointId,
               destinationEndpointId,
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
    if (missingCells || !consistent) {
      result.diagnostics.push_back(
          {portalLoop->layerId, portalLoop->loopId,
           missingCells ? PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint
                        : PortalLiquidDiagnostic::ContradictoryElevationCycle,
           portalLoop->loopId == IndependentPortalLoopId
               ? portalLoop->traversalOrder.front() : ~0u});
      continue;
    }
    offsets = std::move(trial);
    result.adjacency.insert(result.adjacency.end(), hops.begin(), hops.end());
  }
  return result;
}

}  // namespace bw::core
