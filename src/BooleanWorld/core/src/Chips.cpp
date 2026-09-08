#include "core/Chips.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

#include <mapbox/earcut.hpp>

namespace bw::core::arr {
namespace {
// Below this, in world units, a Chip has nothing left to cut and is dropped
// rather than emitted as a sliver.
constexpr float MinimumChipSize = 0.01f;
constexpr float ConcavityEpsilon = 0.00001f;
constexpr float MaximumChipReach = 4.0f;  // Width is half-reach, capped at 2.

// Matches BuildArrangementTriangles' floor/ceiling UV scale, so a rebuilt
// face's texture keeps running through it unbroken.
constexpr float HorizontalUvScale = 64.0f;
constexpr float WedgeCentralLineConvexity = 0.1f;

uint64_t Mix(uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

uint64_t StableArrisSeed(FixedPointVertex a, FixedPointVertex b) {
  if (b.x < a.x || (b.x == a.x && b.y < a.y)) {
    std::swap(a, b);
  }
  auto seed = Mix(static_cast<uint64_t>(a.x));
  seed ^= Mix(static_cast<uint64_t>(a.y) + 0x243f6a8885a308d3ull);
  seed ^= Mix(static_cast<uint64_t>(b.x) + 0x13198a2e03707344ull);
  seed ^= Mix(static_cast<uint64_t>(b.y) + 0xa4093822299f31d0ull);
  return Mix(seed);
}

float StableRandom01(uint64_t seed, uint64_t stream) {
  auto value = Mix(seed ^ Mix(stream));
  return float(value >> 40) / float(uint64_t{1} << 24);
}

uint64_t StableVerticalArrisSeed(
    FixedPointVertex const& vertex, float minZ, float maxZ) {
  auto seed = Mix(static_cast<uint64_t>(vertex.x));
  seed ^= Mix(static_cast<uint64_t>(vertex.y) + 0x243f6a8885a308d3ull);
  seed ^= Mix(uint64_t{std::bit_cast<uint32_t>(minZ)} + 0x13198a2e03707344ull);
  seed ^= Mix(uint64_t{std::bit_cast<uint32_t>(maxZ)} + 0xa4093822299f31d0ull);
  return Mix(seed);
}

uint64_t StableCornerSeed(
    FixedPointVertex const& vertex, float z, ArrangementWallKind kind) {
  auto seed = Mix(static_cast<uint64_t>(vertex.x));
  seed ^= Mix(static_cast<uint64_t>(vertex.y) + 0x243f6a8885a308d3ull);
  seed ^= Mix(uint64_t{std::bit_cast<uint32_t>(z)} + 0x13198a2e03707344ull);
  seed ^= Mix(static_cast<uint64_t>(kind) + 0xa4093822299f31d0ull);
  return Mix(seed);
}

struct GeneratedChip {
  float centre;
  float depth;
  float reach;
  ChipType type;
  uint64_t variationSeed;
};

struct ChipProfileSample {
  float t;
  float sideA;
  float sideB;
};

std::vector<ChipProfileSample> BuildChipProfile(GeneratedChip const& chip) {
  switch (chip.type) {
    case ChipType::Tapered:
      return {{0.0f, 0.0f, 0.0f},
              {0.5f, 1.0f, 1.0f},
              {1.0f, 0.0f, 0.0f}};

    case ChipType::PyramidalDivot:
      return {{0.0f, 0.0f, 0.0f},
              {0.5f, 0.65f, 0.65f},
              {1.0f, 0.0f, 0.0f}};

    case ChipType::VShapedNotch:
      return {{0.0f, 0.0f, 0.0f},
              {0.2f, 0.65f, 0.65f},
              {0.8f, 0.65f, 0.65f},
              {1.0f, 0.0f, 0.0f}};

    case ChipType::PrismaticNotch:
      return {{0.0f, 0.0f, 0.0f},
              {0.0f, 1.0f, 1.0f},
              {1.0f, 1.0f, 1.0f},
              {1.0f, 0.0f, 0.0f}};

    case ChipType::MultiFacetSpall: {
      auto a0 = 0.55f + 0.4f * StableRandom01(chip.variationSeed, 1);
      auto b0 = 0.55f + 0.4f * StableRandom01(chip.variationSeed, 2);
      auto a1 = 0.55f + 0.4f * StableRandom01(chip.variationSeed, 3);
      auto b1 = 0.55f + 0.4f * StableRandom01(chip.variationSeed, 4);
      return {{0.0f, 0.0f, 0.0f},
              {0.2f, a0, b0},
              {0.48f, 1.0f, 0.8f},
              {0.76f, a1, b1},
              {1.0f, 0.0f, 0.0f}};
    }

    case ChipType::SteppedFracture:
      return {{0.0f, 0.0f, 0.0f},
              {0.14f, 0.45f, 0.45f},
              {0.38f, 0.45f, 0.45f},
              {0.38f, 1.0f, 1.0f},
              {0.68f, 1.0f, 1.0f},
              {0.68f, 0.45f, 0.45f},
              {0.88f, 0.45f, 0.45f},
              {1.0f, 0.0f, 0.0f}};

    case ChipType::TrapezoidalSpall:
      return {{0.0f, 0.0f, 0.0f},
              {0.16f, 1.0f, 0.32f},
              {0.5f, 1.0f, 1.0f},
              {0.84f, 1.0f, 0.32f},
              {1.0f, 0.0f, 0.0f}};
  }
  return {};
}

std::vector<GeneratedChip> GenerateChips(
    float length,
    ChipGenerationParameters const& parameters,
    uint64_t seed,
    float startClearance = 0.0f,
    float endClearance = 0.0f) {
  std::vector<GeneratedChip> chips;
  auto availableLength = length - startClearance - endClearance;
  if (parameters.probability <= 0.0f || parameters.types.empty() ||
      length < parameters.minimumArrisLength ||
      availableLength <= MinimumChipSize) {
    return chips;
  }

  auto authoredMaximumReach =
      std::min(parameters.maximumReach, MaximumChipReach);
  auto authoredMinimumReach =
      std::min(parameters.minimumReach, authoredMaximumReach);
  auto maximumReach = std::min(authoredMaximumReach, availableLength);
  auto minimumSpacing =
      std::max(parameters.minimumSpacing, authoredMaximumReach + 0.1f);
  auto usableForCentres =
      std::max(0.0f, availableLength - maximumReach);
  auto maximumCount =
      uint32_t(std::floor(usableForCentres / minimumSpacing)) + 1;
  uint32_t chipCount = 0;
  for (uint32_t slot = 0; slot < maximumCount; ++slot) {
    if (StableRandom01(seed, 0x1000ull + slot) < parameters.probability) {
      ++chipCount;
    }
  }
  if (chipCount == 0) {
    return chips;
  }

  auto slack =
      usableForCentres - float(chipCount - 1) * minimumSpacing;
  std::vector<float> offsets;
  offsets.reserve(chipCount);
  for (uint32_t chip = 0; chip < chipCount; ++chip) {
    offsets.push_back(
        chipCount == 1 ? 0.5f : StableRandom01(seed, 0x2000ull + chip));
  }
  std::sort(offsets.begin(), offsets.end());

  chips.reserve(chipCount);
  for (uint32_t chip = 0; chip < chipCount; ++chip) {
    auto centre = startClearance + maximumReach * 0.5f +
                  float(chip) * minimumSpacing + offsets[chip] * slack;
    auto depth = parameters.minimumDepth +
                 (parameters.maximumDepth - parameters.minimumDepth) *
                     StableRandom01(seed, 0x3000ull + chip);
    auto reach = authoredMinimumReach +
                 (authoredMaximumReach - authoredMinimumReach) *
                     StableRandom01(seed, 0x4000ull + chip);
    auto typeIndex = std::min(
        size_t(StableRandom01(seed, 0x5000ull + chip) *
               float(parameters.types.size())),
        parameters.types.size() - 1);
    chips.push_back(
        {centre, depth, std::min(reach, availableLength),
         parameters.types[typeIndex], Mix(seed ^ uint64_t(chip))});
  }
  return chips;
}

std::vector<float> GenerateWedgeCentres(
    float length,
    float minimumReach,
    float averagePerUnitDistance,
    uint64_t seed,
    uint64_t stream) {
  std::vector<float> centres;
  if (averagePerUnitDistance <= 0.0f || length < minimumReach) {
    return centres;
  }
  auto expectedCount = length * averagePerUnitDistance;
  auto count = uint32_t(std::floor(expectedCount + 0.5f));
  if (count == 0) return centres;

  auto margin = minimumReach * 0.5f;
  auto usable = length - minimumReach;
  centres.reserve(count);
  if (count == 1) {
    centres.push_back(length * 0.5f);
    return centres;
  }
  for (uint32_t candidate = 0; candidate < count; ++candidate) {
    auto offset = StableRandom01(seed, stream + 1 + candidate);
    centres.push_back(
        margin + (float(candidate) + offset) * usable / float(count));
  }
  return centres;
}

struct Vertex3 {
  float x, y, z;
};

wp::Vector2 ToWorld(FixedPointVertex const& vertex) {
  return {ToWorldCoordinate(vertex.x), ToWorldCoordinate(vertex.y)};
}

Vertex3 Cross(Vertex3 const& a, Vertex3 const& b) {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x};
}

float Dot(Vertex3 const& a, Vertex3 const& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Emits one replacement triangle with a flat face normal calculated from its
// geometry, ordering its vertices counter-clockwise about that normal.
// `reference` only picks which of the two opposed normals faces out of the
// solid. Every vertex receives the same normal: these are face normals carried
// through a vertex-attribute rendering API, not smoothed vertex normals.
void AddTriangle(
    DetailGeometry& detail,
    DetailSurfaceKey const& source,
    Vertex3 const& p0,
    Vertex3 const& p1,
    Vertex3 const& p2,
    Vertex3 const& reference,
    std::array<float, 2> const& uv0,
    std::array<float, 2> const& uv1,
    std::array<float, 2> const& uv2,
    bool followsWallFacing = false,
    DetailTriangleKind kind = DetailTriangleKind::SurfaceRemainder,
    std::optional<ChipType> chipType = std::nullopt) {
  auto geometric = Cross(
      {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z},
      {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z});
  auto scale = std::sqrt(Dot(geometric, geometric));
  if (scale <= 0.0f) {
    return;  // Degenerate - nothing to draw.
  }

  auto facing = Dot(geometric, reference);
  auto sign = facing < 0.0f ? -1.0f : 1.0f;
  std::array<float, 3> normal{
      sign * geometric.x / scale,
      sign * geometric.y / scale,
      sign * geometric.z / scale};

  DetailVertex a{{p0.x, p0.y, p0.z}, normal, uv0};
  DetailVertex b{{p1.x, p1.y, p1.z}, normal, uv1};
  DetailVertex c{{p2.x, p2.y, p2.z}, normal, uv2};
  if (facing < 0.0f) {
    std::swap(b, c);
  }
  detail.addTriangle(
      {source, {a, b, c}, kind, followsWallFacing, chipType});
}

// Recursively subdivides a Wedge facet through a displaced centroid. Each
// level replaces one triangle with three; boundary edges remain untouched, so
// independently tessellated facets cannot crack apart.
void AddWedgeTriangle(
    DetailGeometry& detail,
    DetailSurfaceKey const& source,
    Vertex3 const& p0,
    Vertex3 const& p1,
    Vertex3 const& p2,
    Vertex3 const& reference,
    std::array<float, 2> const& uv0,
    std::array<float, 2> const& uv1,
    std::array<float, 2> const& uv2,
    uint32_t quality,
    uint64_t seed,
    uint64_t stream) {
  if (quality == 0) {
    AddTriangle(
        detail, source, p0, p1, p2, reference, uv0, uv1, uv2, false,
        DetailTriangleKind::WedgeFacet);
    return;
  }

  auto edge01 = Vertex3{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  auto edge02 = Vertex3{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
  auto geometric = Cross(edge01, edge02);
  auto normalLength = std::sqrt(Dot(geometric, geometric));
  if (normalLength <= 0.0f) return;
  auto sign = Dot(geometric, reference) < 0.0f ? -1.0f : 1.0f;
  auto normal = Vertex3{
      sign * geometric.x / normalLength,
      sign * geometric.y / normalLength,
      sign * geometric.z / normalLength};
  auto edgeLength = [](Vertex3 const& a, Vertex3 const& b) {
    auto delta = Vertex3{b.x - a.x, b.y - a.y, b.z - a.z};
    return std::sqrt(Dot(delta, delta));
  };
  auto averageEdgeLength =
      (edgeLength(p0, p1) + edgeLength(p1, p2) + edgeLength(p2, p0)) /
      3.0f;
  auto displacement =
      StableRandom01(seed, stream) * averageEdgeLength * 0.05f;
  auto centre = Vertex3{
      (p0.x + p1.x + p2.x) / 3.0f + normal.x * displacement,
      (p0.y + p1.y + p2.y) / 3.0f + normal.y * displacement,
      (p0.z + p1.z + p2.z) / 3.0f + normal.z * displacement};
  auto centreUv = std::array<float, 2>{
      (uv0[0] + uv1[0] + uv2[0]) / 3.0f,
      (uv0[1] + uv1[1] + uv2[1]) / 3.0f};
  auto nextQuality = quality - 1;
  AddWedgeTriangle(
      detail, source, p0, p1, centre, reference, uv0, uv1, centreUv,
      nextQuality, seed, Mix(stream ^ 0x91ull));
  AddWedgeTriangle(
      detail, source, p1, p2, centre, reference, uv1, uv2, centreUv,
      nextQuality, seed, Mix(stream ^ 0xa3ull));
  AddWedgeTriangle(
      detail, source, p2, p0, centre, reference, uv2, uv0, centreUv,
      nextQuality, seed, Mix(stream ^ 0xb5ull));
}

// One Chip's footprint on the horizontal face it bit into: the two points
// where it meets the Arris, and the apex it reaches to inside the face.
struct Footprint {
  uint32_t edgeIndex;
  wp::Vector2 a;
  wp::Vector2 b;
  wp::Vector2 apex;
  std::vector<wp::Vector2> profile;
};

// A trihedral Corner Chip replaces one horizontal boundary vertex with one
// point on each of its incident arrangement edges.
struct FaceCornerCut {
  uint32_t vertexIndex;
  uint32_t edgeA;
  wp::Vector2 pointA;
  uint32_t edgeB;
  wp::Vector2 pointB;

  [[nodiscard]] wp::Vector2 const& pointOn(uint32_t edge) const {
    return edge == edgeA ? pointA : pointB;
  }
};

// One triangular notch in a wall boundary. start/end run in increasing local
// x for horizontal boundaries and increasing z for vertical boundaries.
struct WallNotchProfilePoint {
  float position;
  float depth;
};

struct WallNotch {
  float start;
  float end;
  float depth;
  std::vector<WallNotchProfilePoint> profile;
};

struct WallCornerCut {
  float horizontalDistance;
  float verticalDistance;
};

struct WallNotches {
  std::vector<WallNotch> bottom;
  std::vector<WallNotch> right;
  std::vector<WallNotch> top;
  std::vector<WallNotch> left;
  std::optional<WallCornerCut> bottomLeft;
  std::optional<WallCornerCut> bottomRight;
  std::optional<WallCornerCut> topRight;
  std::optional<WallCornerCut> topLeft;

  [[nodiscard]] bool empty() const {
    return bottom.empty() && right.empty() && top.empty() && left.empty() &&
           !bottomLeft && !bottomRight && !topRight && !topLeft;
  }
};

struct IncidentWall {
  uint32_t wallIndex;
  bool atStart;
  wp::Vector2 ray;
  wp::Vector2 normal;
  float length;
};

std::optional<float> SharedFrontSideAngle(
    IncidentWall const& a, IncidentWall const& b) {
  auto rayCross = a.ray.x * b.ray.y - a.ray.y * b.ray.x;
  if (std::abs(rayCross) <= ConcavityEpsilon) {
    return std::nullopt;
  }
  auto aFacesMinor = a.normal.dot(b.ray);
  auto bFacesMinor = b.normal.dot(a.ray);
  auto rayDot = std::clamp(a.ray.dot(b.ray), -1.0f, 1.0f);
  auto minorAngle = std::acos(rayDot) * (180.0f / std::acos(-1.0f));
  if (aFacesMinor > ConcavityEpsilon &&
      bFacesMinor > ConcavityEpsilon) {
    return minorAngle;
  }
  if (aFacesMinor < -ConcavityEpsilon &&
      bFacesMinor < -ConcavityEpsilon) {
    return 360.0f - minorAngle;
  }
  return std::nullopt;
}

bool IsEligibleVerticalAngle(IncidentWall const& a, IncidentWall const& b) {
  auto angle = SharedFrontSideAngle(a, b);
  return angle && *angle >= 225.0f - ConcavityEpsilon &&
         *angle <= 315.0f + ConcavityEpsilon;
}

struct NotchBounds {
  float minX;
  float maxX;
  float minZ;
  float maxZ;
};

bool BoundsOverlap(NotchBounds const& a, NotchBounds const& b) {
  return std::min(a.maxX, b.maxX) - std::max(a.minX, b.minX) >
             MinimumChipSize &&
         std::min(a.maxZ, b.maxZ) - std::max(a.minZ, b.minZ) >
             MinimumChipSize;
}

// A Vertical Chip is generated after Horizontal Chips. Reject it when its
// wall footprint would overlap any notch already accepted on that wall;
// otherwise adjacent-Arris sawteeth could make the remainder polygon cross
// itself at a corner.
bool VerticalNotchFits(
    WallNotches const& existing,
    bool atStart,
    WallNotch const& proposed,
    float length,
    float minZ,
    float maxZ) {
  NotchBounds candidate{
      atStart ? 0.0f : length - proposed.depth,
      atStart ? proposed.depth : length,
      proposed.start,
      proposed.end};
  auto noneOverlap = [&](std::vector<WallNotch> const& notches,
                         auto bounds) {
    return std::none_of(
        notches.begin(), notches.end(), [&](WallNotch const& notch) {
          return BoundsOverlap(candidate, bounds(notch));
        });
  };
  return noneOverlap(existing.bottom, [&](WallNotch const& notch) {
           return NotchBounds{
               notch.start, notch.end, minZ, minZ + notch.depth};
         }) &&
         noneOverlap(existing.top, [&](WallNotch const& notch) {
           return NotchBounds{
               notch.start, notch.end, maxZ - notch.depth, maxZ};
         }) &&
         noneOverlap(existing.left, [&](WallNotch const& notch) {
           return NotchBounds{0.0f, notch.depth, notch.start, notch.end};
         }) &&
         noneOverlap(existing.right, [&](WallNotch const& notch) {
           return NotchBounds{
               length - notch.depth, length, notch.start, notch.end};
         });
}

// Rebuilds a wall quad with every triangular notch removed from any of its
// four Arrises. Earcut handles the resulting sawtooth boundary as one polygon.
void AddWallRemainder(
    DetailGeometry& detail,
    DetailSurfaceKey const& source,
    wp::Vector2 const& v0,
    wp::Vector2 const& direction,
    Vertex3 const& reference,
    float length,
    float minZ,
    float maxZ,
    WallNotches const& notches) {
  using EarcutPoint = std::array<double, 2>;
  std::vector<std::vector<EarcutPoint>> polygon(1);
  std::vector<std::array<float, 2>> localPositions;
  auto push = [&](float distance, float z) {
    if (!localPositions.empty() &&
        std::abs(localPositions.back()[0] - distance) <= MinimumChipSize &&
        std::abs(localPositions.back()[1] - z) <= MinimumChipSize) {
      return;
    }
    polygon.front().push_back({double(distance), double(z)});
    localPositions.push_back({distance, z});
  };

  // Counter-clockwise around the wall's local (distance, z) rectangle.
  // A Corner Chip replaces a rectangle corner with the segment joining its
  // independently chosen horizontal and vertical edge points.
  push(notches.bottomLeft ? notches.bottomLeft->horizontalDistance : 0.0f,
       minZ);
  for (auto const& notch : notches.bottom) {
    if (notch.profile.empty()) {
      push(notch.start, minZ);
      push((notch.start + notch.end) * 0.5f, minZ + notch.depth);
      push(notch.end, minZ);
    } else {
      for (auto const& point : notch.profile) {
        push(point.position, minZ + point.depth);
      }
    }
  }
  push(notches.bottomRight
           ? length - notches.bottomRight->horizontalDistance
           : length,
       minZ);
  if (notches.bottomRight) {
    push(length, minZ + notches.bottomRight->verticalDistance);
  }
  for (auto const& notch : notches.right) {
    if (notch.profile.empty()) {
      push(length, notch.start);
      push(length - notch.depth, (notch.start + notch.end) * 0.5f);
      push(length, notch.end);
    } else {
      for (auto const& point : notch.profile) {
        push(length - point.depth, point.position);
      }
    }
  }
  push(length,
       notches.topRight ? maxZ - notches.topRight->verticalDistance : maxZ);
  if (notches.topRight) {
    push(length - notches.topRight->horizontalDistance, maxZ);
  }
  for (auto it = notches.top.rbegin(); it != notches.top.rend(); ++it) {
    if (it->profile.empty()) {
      push(it->end, maxZ);
      push((it->start + it->end) * 0.5f, maxZ - it->depth);
      push(it->start, maxZ);
    } else {
      for (auto point = it->profile.rbegin(); point != it->profile.rend();
           ++point) {
        push(point->position, maxZ - point->depth);
      }
    }
  }
  push(notches.topLeft ? notches.topLeft->horizontalDistance : 0.0f, maxZ);
  if (notches.topLeft) {
    push(0.0f, maxZ - notches.topLeft->verticalDistance);
  }
  for (auto it = notches.left.rbegin(); it != notches.left.rend(); ++it) {
    if (it->profile.empty()) {
      push(0.0f, it->end);
      push(it->depth, (it->start + it->end) * 0.5f);
      push(0.0f, it->start);
    } else {
      for (auto point = it->profile.rbegin(); point != it->profile.rend();
           ++point) {
        push(point->depth, point->position);
      }
    }
  }
  push(0.0f,
       notches.bottomLeft ? minZ + notches.bottomLeft->verticalDistance
                          : minZ);

  auto indices = mapbox::earcut<uint32_t>(polygon);
  auto height = maxZ - minZ;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vertex3 vertices[3];
    std::array<float, 2> uv[3];
    for (int corner = 0; corner < 3; ++corner) {
      auto const& local = localPositions[indices[i + corner]];
      auto position = v0 + direction * local[0];
      vertices[corner] = {position.x, position.y, local[1]};
      uv[corner] = {local[0] / length, (local[1] - minZ) / height};
    }
    AddTriangle(
        detail, source, vertices[0], vertices[1], vertices[2], reference,
        uv[0], uv[1], uv[2], true);
  }
}

constexpr float FootprintEpsilon = 0.0001f;

float Cross2d(
    wp::Vector2 const& a,
    wp::Vector2 const& b,
    wp::Vector2 const& c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

bool PointOnSegment(
    wp::Vector2 const& point,
    wp::Vector2 const& a,
    wp::Vector2 const& b) {
  if (std::abs(Cross2d(a, b, point)) > FootprintEpsilon) return false;
  return point.x >= std::min(a.x, b.x) - FootprintEpsilon &&
         point.x <= std::max(a.x, b.x) + FootprintEpsilon &&
         point.y >= std::min(a.y, b.y) - FootprintEpsilon &&
         point.y <= std::max(a.y, b.y) + FootprintEpsilon;
}

enum struct RingPointLocation { Outside, Inside, Boundary };

RingPointLocation PointInRing(
    ArrangementResult const& arrangement,
    std::vector<uint32_t> const& vertices,
    wp::Vector2 const& point) {
  bool inside = false;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto a = ToWorld(arrangement.vertices[vertices[i]]);
    auto b = ToWorld(
        arrangement.vertices[vertices[(i + 1) % vertices.size()]]);
    if (PointOnSegment(point, a, b)) return RingPointLocation::Boundary;
    if ((a.y > point.y) != (b.y > point.y)) {
      auto crossingX =
          a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (crossingX > point.x) inside = !inside;
    }
  }
  return inside ? RingPointLocation::Inside : RingPointLocation::Outside;
}

bool PointInFaceClosure(
    ArrangementResult const& arrangement,
    ArrangementFace const& face,
    wp::Vector2 const& point) {
  auto outer = PointInRing(arrangement, face.outerBoundaryVertices, point);
  if (outer == RingPointLocation::Boundary) return true;
  if (outer != RingPointLocation::Inside) return false;
  for (auto const& hole : face.innerBoundaryVertices) {
    auto location = PointInRing(arrangement, hole, point);
    if (location == RingPointLocation::Boundary) return true;
    if (location == RingPointLocation::Inside) return false;
  }
  return true;
}

bool PointStrictlyInTriangle(
    wp::Vector2 const& point,
    std::array<wp::Vector2, 3> const& triangle) {
  auto a = Cross2d(triangle[0], triangle[1], point);
  auto b = Cross2d(triangle[1], triangle[2], point);
  auto c = Cross2d(triangle[2], triangle[0], point);
  return (a > FootprintEpsilon && b > FootprintEpsilon &&
          c > FootprintEpsilon) ||
         (a < -FootprintEpsilon && b < -FootprintEpsilon &&
          c < -FootprintEpsilon);
}

bool SegmentEntersTriangleInterior(
    wp::Vector2 const& a,
    wp::Vector2 const& b,
    std::array<wp::Vector2, 3> const& triangle);

bool PointStrictlyInPolygon(
    wp::Vector2 const& point,
    std::vector<wp::Vector2> const& polygon) {
  bool inside = false;
  for (size_t i = 0; i < polygon.size(); ++i) {
    auto const& a = polygon[i];
    auto const& b = polygon[(i + 1) % polygon.size()];
    if (PointOnSegment(point, a, b)) return false;
    if ((a.y > point.y) != (b.y > point.y)) {
      auto crossingX =
          a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (crossingX > point.x) inside = !inside;
    }
  }
  return inside;
}

bool PolygonInteriorsOverlap(
    std::array<wp::Vector2, 3> const& triangle,
    std::vector<wp::Vector2> const& polygon) {
  if (polygon.size() < 3) return false;
  for (auto const& point : triangle) {
    if (PointStrictlyInPolygon(point, polygon)) return true;
  }
  for (auto const& point : polygon) {
    if (PointStrictlyInTriangle(point, triangle)) return true;
  }
  for (size_t edge = 0; edge < polygon.size(); ++edge) {
    if (SegmentEntersTriangleInterior(
            polygon[edge], polygon[(edge + 1) % polygon.size()], triangle)) {
      return true;
    }
  }

  // Coincident polygons need an interior sample: every edge lies on the
  // candidate boundary, so no boundary segment enters its strict interior.
  auto triangleCentre =
      (triangle[0] + triangle[1] + triangle[2]) * (1.0f / 3.0f);
  return PointStrictlyInPolygon(triangleCentre, polygon);
}

bool SegmentEntersTriangleInterior(
    wp::Vector2 const& a,
    wp::Vector2 const& b,
    std::array<wp::Vector2, 3> const& triangle) {
  if (PointStrictlyInTriangle(a, triangle) ||
      PointStrictlyInTriangle(b, triangle)) {
    return true;
  }

  // Split the boundary segment wherever it meets a triangle side, then test
  // each open interval. This also catches a segment that enters exactly at a
  // triangle vertex, which a strict proper-intersection test would miss.
  std::vector<float> parameters{0.0f, 1.0f};
  auto segment = b - a;
  for (size_t side = 0; side < triangle.size(); ++side) {
    auto c = triangle[side];
    auto edge = triangle[(side + 1) % triangle.size()] - c;
    auto denominator = segment.x * edge.y - segment.y * edge.x;
    if (std::abs(denominator) <= FootprintEpsilon) continue;
    auto offset = c - a;
    auto t = (offset.x * edge.y - offset.y * edge.x) / denominator;
    auto u = (offset.x * segment.y - offset.y * segment.x) / denominator;
    if (t >= -FootprintEpsilon && t <= 1.0f + FootprintEpsilon &&
        u >= -FootprintEpsilon && u <= 1.0f + FootprintEpsilon) {
      parameters.push_back(std::clamp(t, 0.0f, 1.0f));
    }
  }
  std::sort(parameters.begin(), parameters.end());
  for (size_t i = 0; i + 1 < parameters.size(); ++i) {
    if (parameters[i + 1] - parameters[i] <= FootprintEpsilon) continue;
    auto t = (parameters[i] + parameters[i + 1]) * 0.5f;
    if (PointStrictlyInTriangle(a + segment * t, triangle)) return true;
  }
  return false;
}

// A footprint is valid only when its complete closed triangle lies in the
// face. Besides checking its vertices, inspect every outer/hole boundary for
// a segment entering the triangle's interior. This catches crossings,
// re-entrant notches, and holes wholly contained by a large footprint.
bool WedgeFootprintFits(
    ArrangementResult const& arrangement,
    ArrangementFace const& face,
    std::array<wp::Vector2, 3> const& triangle) {
  for (auto const& point : triangle) {
    if (!PointInFaceClosure(arrangement, face, point)) return false;
  }

  auto boundaryFits = [&](std::vector<uint32_t> const& vertices) {
    for (size_t i = 0; i < vertices.size(); ++i) {
      auto a = ToWorld(arrangement.vertices[vertices[i]]);
      auto b = ToWorld(
          arrangement.vertices[vertices[(i + 1) % vertices.size()]]);
      if (SegmentEntersTriangleInterior(a, b, triangle)) return false;
    }
    return true;
  };

  if (!boundaryFits(face.outerBoundaryVertices)) {
    return false;
  }
  auto holes = std::min(
      face.innerBoundaries.size(), face.innerBoundaryVertices.size());
  for (size_t hole = 0; hole < holes; ++hole) {
    if (!boundaryFits(face.innerBoundaryVertices[hole])) {
      return false;
    }
  }
  return true;
}

bool ArrisIntervalsOverlap(
    wp::Vector2 const& wedgeA,
    wp::Vector2 const& wedgeB,
    wp::Vector2 const& reservationA,
    wp::Vector2 const& reservationB) {
  auto direction = (wedgeB - wedgeA).normalisedCopy();
  auto wedgeLength = wedgeA.distanceTo(wedgeB);
  auto start = (reservationA - wedgeA).dot(direction);
  auto end = (reservationB - wedgeA).dot(direction);
  return std::min(std::max(start, end), wedgeLength) -
             std::max(std::min(start, end), 0.0f) >
         FootprintEpsilon;
}

bool WedgeHorizontalSurfaceAvoidsChips(
    ArrangementResult const& arrangement,
    uint32_t wallEdge,
    std::array<wp::Vector2, 3> const& triangle,
    std::vector<Footprint> const& footprints,
    std::vector<FaceCornerCut> const& cornerCuts) {
  auto const& wedgeA = triangle[0];
  auto const& wedgeB = triangle[1];
  for (auto const& footprint : footprints) {
    if (footprint.edgeIndex == wallEdge &&
        ArrisIntervalsOverlap(
            wedgeA, wedgeB, footprint.a, footprint.b)) {
      return false;
    }
    auto reservation = footprint.profile.empty()
                           ? std::vector<wp::Vector2>{
                                 footprint.a, footprint.apex, footprint.b}
                           : footprint.profile;
    if (PolygonInteriorsOverlap(triangle, reservation)) return false;
  }
  for (auto const& cut : cornerCuts) {
    auto corner = ToWorld(arrangement.vertices[cut.vertexIndex]);
    if (PolygonInteriorsOverlap(
            triangle, {corner, cut.pointA, cut.pointB})) {
      return false;
    }
  }
  return true;
}

bool CornerWedgeHorizontalSurfaceAvoidsChips(
    ArrangementResult const& arrangement,
    uint32_t edgeA,
    uint32_t edgeB,
    std::array<wp::Vector2, 3> const& triangle,
    std::vector<Footprint> const& footprints,
    std::vector<FaceCornerCut> const& cornerCuts) {
  for (auto const& footprint : footprints) {
    auto overlapsA = footprint.edgeIndex == edgeA &&
                     ArrisIntervalsOverlap(
                         triangle[0], triangle[1], footprint.a, footprint.b);
    auto overlapsB = footprint.edgeIndex == edgeB &&
                     ArrisIntervalsOverlap(
                         triangle[0], triangle[2], footprint.a, footprint.b);
    if (overlapsA || overlapsB) return false;
    auto reservation = footprint.profile.empty()
                           ? std::vector<wp::Vector2>{
                                 footprint.a, footprint.apex, footprint.b}
                           : footprint.profile;
    if (PolygonInteriorsOverlap(triangle, reservation)) return false;
  }
  for (auto const& cut : cornerCuts) {
    auto corner = ToWorld(arrangement.vertices[cut.vertexIndex]);
    if (PolygonInteriorsOverlap(
            triangle, {corner, cut.pointA, cut.pointB})) {
      return false;
    }
  }
  return true;
}

std::vector<std::vector<wp::Vector2>> WallChipReservations(
    WallNotches const& notches,
    float length,
    float minZ,
    float maxZ) {
  std::vector<std::vector<wp::Vector2>> reservations;
  auto height = maxZ - minZ;
  for (auto const& notch : notches.bottom) {
    std::vector<wp::Vector2> polygon;
    if (notch.profile.empty()) {
      polygon = {{notch.start, height},
                 {(notch.start + notch.end) * 0.5f,
                  height - notch.depth},
                 {notch.end, height}};
    } else {
      for (auto const& point : notch.profile) {
        polygon.push_back({point.position, height - point.depth});
      }
    }
    reservations.push_back(std::move(polygon));
  }
  for (auto const& notch : notches.top) {
    std::vector<wp::Vector2> polygon;
    if (notch.profile.empty()) {
      polygon = {{notch.start, 0.0f},
                 {(notch.start + notch.end) * 0.5f, notch.depth},
                 {notch.end, 0.0f}};
    } else {
      for (auto const& point : notch.profile) {
        polygon.push_back({point.position, point.depth});
      }
    }
    reservations.push_back(std::move(polygon));
  }
  for (auto const& notch : notches.left) {
    std::vector<wp::Vector2> polygon;
    if (notch.profile.empty()) {
      polygon = {{0.0f, maxZ - notch.start},
                 {notch.depth,
                  maxZ - (notch.start + notch.end) * 0.5f},
                 {0.0f, maxZ - notch.end}};
    } else {
      for (auto const& point : notch.profile) {
        polygon.push_back({point.depth, maxZ - point.position});
      }
    }
    reservations.push_back(std::move(polygon));
  }
  for (auto const& notch : notches.right) {
    std::vector<wp::Vector2> polygon;
    if (notch.profile.empty()) {
      polygon = {{length, maxZ - notch.start},
                 {length - notch.depth,
                  maxZ - (notch.start + notch.end) * 0.5f},
                 {length, maxZ - notch.end}};
    } else {
      for (auto const& point : notch.profile) {
        polygon.push_back({length - point.depth, maxZ - point.position});
      }
    }
    reservations.push_back(std::move(polygon));
  }
  auto addCorner = [&](std::optional<WallCornerCut> const& cut,
                       float x,
                       float y,
                       float horizontalSign,
                       float verticalSign) {
    if (!cut) return;
    reservations.push_back({
        {x, y},
        {x + horizontalSign * cut->horizontalDistance, y},
        {x, y + verticalSign * cut->verticalDistance}});
  };
  addCorner(notches.topLeft, 0.0f, 0.0f, 1.0f, 1.0f);
  addCorner(notches.topRight, length, 0.0f, -1.0f, 1.0f);
  addCorner(notches.bottomLeft, 0.0f, height, 1.0f, -1.0f);
  addCorner(notches.bottomRight, length, height, -1.0f, -1.0f);
  return reservations;
}

bool WedgeWallAttachmentAvoidsChips(
    float arrisStart,
    float arrisEnd,
    std::array<wp::Vector2, 3> const& attachment,
    float length,
    bool atTop,
    WallNotches const& notches,
    std::vector<std::vector<wp::Vector2>> const& reservations) {
  auto start = std::min(arrisStart, arrisEnd);
  auto end = std::max(arrisStart, arrisEnd);
  auto const& arrisNotches = atTop ? notches.top : notches.bottom;
  for (auto const& notch : arrisNotches) {
    if (std::min(end, notch.end) - std::max(start, notch.start) >
        FootprintEpsilon) {
      return false;
    }
  }
  auto const& startCorner = atTop ? notches.topLeft : notches.bottomLeft;
  auto const& endCorner = atTop ? notches.topRight : notches.bottomRight;
  if (startCorner &&
      std::min(end, startCorner->horizontalDistance) - start >
          FootprintEpsilon) {
    return false;
  }
  if (endCorner &&
      end - std::max(start, length - endCorner->horizontalDistance) >
          FootprintEpsilon) {
    return false;
  }
  return std::none_of(
      reservations.begin(), reservations.end(), [&](auto const& reservation) {
        return PolygonInteriorsOverlap(attachment, reservation);
      });
}

bool WedgeWallAvoidsChips(
    float centre,
    float reach,
    float verticalExtent,
    float length,
    float height,
    bool atTop,
    WallNotches const& notches,
    std::vector<std::vector<wp::Vector2>> const& reservations) {
  auto start = centre - reach * 0.5f;
  auto end = centre + reach * 0.5f;
  auto arrisY = atTop ? 0.0f : height;
  auto apexY = atTop ? verticalExtent : height - verticalExtent;
  std::array<wp::Vector2, 3> attachment{
      wp::Vector2{start, arrisY}, wp::Vector2{end, arrisY},
      wp::Vector2{centre, apexY}};
  return WedgeWallAttachmentAvoidsChips(
      start, end, attachment, length, atTop, notches, reservations);
}

template <typename Predicate>
std::optional<float> CapMonotonicRange(
    float minimum,
    float maximum,
    Predicate&& fits) {
  if (!fits(minimum)) return std::nullopt;
  if (fits(maximum)) return maximum;
  auto low = minimum;
  auto high = maximum;
  for (int iteration = 0; iteration < 32; ++iteration) {
    auto midpoint = (low + high) * 0.5f;
    if (fits(midpoint)) low = midpoint;
    else high = midpoint;
  }
  return low;
}

// How far a Chip may bite into `face` from `origin` (a point on `currentEdge`)
// along `direction` before it would reach some *other* boundary of the face -
// an opposite Arris, a hole, or a wall of the same footprint met at a corner.
// Casts a ray rather than measuring edge-to-edge distance because a Chip's
// deepest point is a single point on the Arris's own perpendicular bisector,
// not the whole Arris; only that ray's first crossing can be broken through.
// Returns a large sentinel when nothing else bounds the face in that
// direction, so callers can std::min it in unconditionally.
float FaceBoundaryDistance(
    ArrangementResult const& arrangement,
    ArrangementFace const& face,
    uint32_t currentEdge,
    wp::Vector2 const& origin,
    wp::Vector2 const& direction) {
  constexpr float NoBoundary = 1.0e9f;
  auto nearest = NoBoundary;

  auto castAgainstBoundary = [&](std::vector<uint32_t> const& boundary,
                                 std::vector<uint32_t> const& boundaryVertices) {
    auto count = boundaryVertices.size();
    for (size_t i = 0; i < count; ++i) {
      if (i < boundary.size() && boundary[i] == currentEdge) {
        continue;
      }
      auto p0 = ToWorld(arrangement.vertices[boundaryVertices[i]]);
      auto p1 =
          ToWorld(arrangement.vertices[boundaryVertices[(i + 1) % count]]);

      // origin + t*direction = p0 + s*(p1-p0); Cramer's rule on the 2x2
      // system, direction and (p1-p0) as its columns.
      auto edge = p1 - p0;
      auto denom = direction.x * edge.y - direction.y * edge.x;
      if (std::abs(denom) <= 1.0e-9f) {
        continue;  // Parallel - a Chip's ray never runs along a boundary.
      }
      auto diff = p0 - origin;
      auto t = (diff.x * edge.y - diff.y * edge.x) / denom;
      auto s = (diff.x * direction.y - diff.y * direction.x) / denom;
      if (t > MinimumChipSize && t < nearest && s >= 0.0f && s <= 1.0f) {
        nearest = t;
      }
    }
  };

  castAgainstBoundary(face.outerBoundary, face.outerBoundaryVertices);
  auto holes =
      std::min(face.innerBoundaries.size(), face.innerBoundaryVertices.size());
  for (size_t hole = 0; hole < holes; ++hole) {
    castAgainstBoundary(face.innerBoundaries[hole], face.innerBoundaryVertices[hole]);
  }

  return nearest;
}

bool IsHorizontal(Elevation const& elevation) {
  return elevation.gradient == wp::Vector2::ZERO;
}

bool SupportsHorizontalDetail(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall) {
  auto const& edge = arrangement.edges[wall.edge];
  auto const& properties0 =
      arrangement.palette[arrangement.faces[edge.face[0]].paletteIndex];
  auto const& properties1 =
      arrangement.palette[arrangement.faces[edge.face[1]].paletteIndex];
  auto surfaceIsHorizontal = [&](PrimitivePropertySet const& properties) {
    return wall.kind == ArrangementWallKind::FloorStep
               ? IsHorizontal(properties.floorZ)
               : IsHorizontal(properties.ceilingZ);
  };
  return surfaceIsHorizontal(properties0) &&
         surfaceIsHorizontal(properties1);
}

bool SupportsWallDetail(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall) {
  auto const& edge = arrangement.edges[wall.edge];
  for (auto faceIndex : edge.face) {
    auto const& face = arrangement.faces[faceIndex];
    if (!face.solid) continue;
    auto const& properties = arrangement.palette[face.paletteIndex];
    if (!IsHorizontal(properties.floorZ) ||
        !IsHorizontal(properties.ceilingZ)) {
      return false;
    }
  }
  return true;
}

// Rebuilds one horizontal face's floor or ceiling with every Chip footprint
// on it subtracted from its boundary polygon, rather than clipping the
// individual triangles the unchipped face earcut to.
void AddRebuiltFaceHorizontal(
    DetailGeometry& detail,
    ArrangementResult const& arrangement,
    DetailSurfaceKey const& source,
    std::vector<Footprint> const& footprints,
    std::vector<FaceCornerCut> const& cornerCuts) {
  using EarcutPoint = std::array<double, 2>;

  auto const& face = arrangement.faces[source.index];
  auto isFloor = source.kind == DetailSurfaceKind::FloorOfFace;
  auto const& properties = arrangement.palette[face.paletteIndex];
  auto z = (isFloor ? properties.floorZ : properties.ceilingZ).baseElevation;

  std::vector<std::vector<EarcutPoint>> polygons;
  // Parallel to earcut's own index space, which runs across every ring in
  // the order they are added.
  std::vector<wp::Vector2> positions;

  auto addBoundary = [&](std::vector<uint32_t> const& boundary,
                         std::vector<uint32_t> const& boundaryVertices) {
    std::vector<EarcutPoint> polygon;
    auto push = [&](wp::Vector2 const& point) {
      if (!polygon.empty()) {
        auto const& previous = polygon.back();
        if (std::abs(previous[0] - point.x) <= MinimumChipSize &&
            std::abs(previous[1] - point.y) <= MinimumChipSize) {
          return;
        }
      }
      polygon.push_back({double(point.x), double(point.y)});
      positions.push_back(point);
    };
    auto pushDistinct = [&](wp::Vector2 const& point,
                            wp::Vector2 const& from,
                            wp::Vector2 const& to) {
      // A Chip reaching the full length of its Arris lands exactly on a
      // boundary vertex; emitting it again would leave earcut a zero-length
      // edge to work with.
      if (point.distanceTo(from) <= MinimumChipSize ||
          point.distanceTo(to) <= MinimumChipSize) {
        return;
      }
      push(point);
    };

    auto count = boundaryVertices.size();
    for (size_t i = 0; i < count; ++i) {
      auto currentVertex = boundaryVertices[i];
      auto current = ToWorld(arrangement.vertices[currentVertex]);
      auto next = ToWorld(
          arrangement.vertices[boundaryVertices[(i + 1) % count]]);
      auto cornerCut = std::find_if(
          cornerCuts.begin(), cornerCuts.end(),
          [&](FaceCornerCut const& cut) {
            return cut.vertexIndex == currentVertex;
          });
      if (cornerCut != cornerCuts.end() && !boundary.empty()) {
        auto previousEdge = boundary[(i + count - 1) % count];
        auto nextEdge = boundary[i];
        push(cornerCut->pointOn(previousEdge));
        push(cornerCut->pointOn(nextEdge));
      } else {
        push(current);
      }
      if (i >= boundary.size()) {
        continue;
      }
      std::vector<Footprint const*> edgeFootprints;
      for (auto const& footprint : footprints) {
        if (footprint.edgeIndex == boundary[i]) {
          edgeFootprints.push_back(&footprint);
        }
      }
      std::sort(
          edgeFootprints.begin(), edgeFootprints.end(),
          [&](Footprint const* a, Footprint const* b) {
            auto aDistance = std::min(
                current.distanceTo(a->a), current.distanceTo(a->b));
            auto bDistance = std::min(
                current.distanceTo(b->a), current.distanceTo(b->b));
            return aDistance < bDistance;
          });
      for (auto const* footprint : edgeFootprints) {
        // The boundary traversal may run either way along the Arris.
        auto aFirst = current.distanceTo(footprint->a) <=
                      current.distanceTo(footprint->b);
        if (footprint->profile.empty()) {
          auto const& first = aFirst ? footprint->a : footprint->b;
          auto const& second = aFirst ? footprint->b : footprint->a;
          pushDistinct(first, current, next);
          push(footprint->apex);
          pushDistinct(second, current, next);
        } else if (aFirst) {
          for (auto const& point : footprint->profile) {
            pushDistinct(point, current, next);
          }
        } else {
          for (auto point = footprint->profile.rbegin();
               point != footprint->profile.rend(); ++point) {
            pushDistinct(*point, current, next);
          }
        }
      }
    }
    polygons.push_back(std::move(polygon));
  };

  addBoundary(face.outerBoundary, face.outerBoundaryVertices);
  auto holes = std::min(
      face.innerBoundaries.size(), face.innerBoundaryVertices.size());
  for (size_t hole = 0; hole < holes; ++hole) {
    addBoundary(face.innerBoundaries[hole], face.innerBoundaryVertices[hole]);
  }

  detail.addSuppressed(source);

  Vertex3 normal{0.0f, 0.0f, isFloor ? 1.0f : -1.0f};
  auto indices = mapbox::earcut<uint32_t>(polygons);
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vertex3 p[3];
    std::array<float, 2> uv[3];
    for (int corner = 0; corner < 3; ++corner) {
      auto const& position = positions[indices[i + corner]];
      p[corner] = {position.x, position.y, z};
      uv[corner] = {
          position.x / HorizontalUvScale, position.y / HorizontalUvScale};
    }
    AddTriangle(detail, source, p[0], p[1], p[2], normal, uv[0], uv[1], uv[2]);
  }
}
}  // namespace

void DetailGeometry::addSuppressed(DetailSurfaceKey const& key) {
  mSuppressed.push_back(key);
}

void DetailGeometry::addTriangle(DetailTriangle const& triangle) {
  mTriangles.push_back(triangle);
}

void DetailGeometry::countChip() {
  ++mChipCount;
}

void DetailGeometry::countWedge() {
  ++mWedgeCount;
}

void DetailGeometry::sort() {
  std::sort(mSuppressed.begin(), mSuppressed.end());
  std::stable_sort(
      mTriangles.begin(), mTriangles.end(),
      [](DetailTriangle const& a, DetailTriangle const& b) {
        return a.source < b.source;
      });
}

bool DetailGeometry::isSuppressed(
    DetailSurfaceKind kind,
    uint32_t index) const {
  return std::binary_search(
      mSuppressed.begin(), mSuppressed.end(), DetailSurfaceKey{kind, index});
}

std::span<DetailTriangle const> DetailGeometry::replacementsFor(
    DetailSurfaceKind kind,
    uint32_t index) const {
  DetailSurfaceKey key{kind, index};
  auto lower = std::lower_bound(
      mTriangles.begin(), mTriangles.end(), key,
      [](DetailTriangle const& triangle, DetailSurfaceKey const& value) {
        return triangle.source < value;
      });
  auto upper = std::upper_bound(
      lower, mTriangles.end(), key,
      [](DetailSurfaceKey const& value, DetailTriangle const& triangle) {
        return value < triangle.source;
      });
  return {
      mTriangles.data() + (lower - mTriangles.begin()),
      size_t(upper - lower)};
}

std::vector<DetailSurfaceKey> const& DetailGeometry::getSuppressed() const {
  return mSuppressed;
}

std::vector<DetailTriangle> const& DetailGeometry::getTriangles() const {
  return mTriangles;
}

uint32_t DetailGeometry::getChipCount() const {
  return mChipCount;
}

uint32_t DetailGeometry::getWedgeCount() const {
  return mWedgeCount;
}

DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls,
    WedgeGenerationParameters const& wedgeParameters) {
  DetailGeometry detail;
  std::map<DetailSurfaceKey, std::vector<Footprint>> footprintsByFace;
  std::map<DetailSurfaceKey, std::vector<FaceCornerCut>> cornerCutsByFace;
  std::vector<WallNotches> wallNotches(walls.size());
  std::vector<float> horizontalStartClearance(walls.size());
  std::vector<float> horizontalEndClearance(walls.size());
  std::map<std::pair<uint32_t, uint32_t>, float> verticalCornerClearance;

  std::map<uint32_t, std::vector<IncidentWall>> wallsByVertex;
  for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
       ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto const& edge = arrangement.edges[wall.edge];
    auto orientation = OrientArrangementWall(arrangement, wall);
    auto along = orientation.v1 - orientation.v0;
    auto length = along.length();
    if (length <= MinimumChipSize) {
      continue;
    }
    auto direction = along / length;
    auto edgeV0 = ToWorld(arrangement.vertices[edge.v[0]]);
    auto startVertex = orientation.v0.distanceTo(edgeV0) <= MinimumChipSize
                           ? edge.v[0]
                           : edge.v[1];
    auto endVertex = startVertex == edge.v[0] ? edge.v[1] : edge.v[0];
    wallsByVertex[startVertex].push_back(
        {wallIndex, true, direction, orientation.normal, length});
    wallsByVertex[endVertex].push_back(
        {wallIndex, false, -direction, orientation.normal, length});
  }

  auto bittenFaceFor = [&](ArrangementWall const& wall)
      -> std::optional<uint32_t> {
    if ((wall.kind != ArrangementWallKind::FloorStep &&
         wall.kind != ArrangementWallKind::CeilingStep) ||
        !SupportsHorizontalDetail(arrangement, wall)) {
      return std::nullopt;
    }
    auto const& edge = arrangement.edges[wall.edge];
    auto const& properties0 =
        arrangement.palette[arrangement.faces[edge.face[0]].paletteIndex];
    auto const& properties1 =
        arrangement.palette[arrangement.faces[edge.face[1]].paletteIndex];
    auto face =
        wall.kind == ArrangementWallKind::FloorStep
            ? (properties0.floorZ.baseElevation >
                       properties1.floorZ.baseElevation
                   ? edge.face[0]
                                                               : edge.face[1])
            : (properties0.ceilingZ.baseElevation <
                       properties1.ceilingZ.baseElevation
                           ? edge.face[0]
                           : edge.face[1]);
    return arrangement.faces[face].solid ? std::optional<uint32_t>{face}
                                         : std::nullopt;
  };

  // Corner Chips truncate a trihedral vertex where two walls and the top of
  // one FloorStep, or the bottom of one CeilingStep, meet. Generate them
  // first so ordinary Arris Chips can reserve the resulting endpoint cuts.
  for (auto const& [vertexIndex, incident] : wallsByVertex) {
    auto corner = ToWorld(arrangement.vertices[vertexIndex]);
    for (size_t aIndex = 0; aIndex < incident.size(); ++aIndex) {
      for (size_t bIndex = aIndex + 1; bIndex < incident.size(); ++bIndex) {
        auto const* aPtr = &incident[aIndex];
        auto const* bPtr = &incident[bIndex];
        if (bPtr->ray.x < aPtr->ray.x ||
            (bPtr->ray.x == aPtr->ray.x && bPtr->ray.y < aPtr->ray.y)) {
          std::swap(aPtr, bPtr);
        }
        auto const& a = *aPtr;
        auto const& b = *bPtr;
        auto const& wallA = walls[a.wallIndex];
        auto const& wallB = walls[b.wallIndex];
        if (wallA.kind != wallB.kind ||
            (wallA.kind != ArrangementWallKind::FloorStep &&
             wallA.kind != ArrangementWallKind::CeilingStep) ||
            !IsEligibleVerticalAngle(a, b)) {
          continue;
        }
        auto faceA = bittenFaceFor(wallA);
        auto faceB = bittenFaceFor(wallB);
        if (!faceA || faceA != faceB) {
          continue;
        }
        auto const& propertiesA = arrangement.palette[wallA.paletteIndex];
        auto const& propertiesB = arrangement.palette[wallB.paletteIndex];
        if (propertiesA.wallMaterialId != propertiesB.wallMaterialId) {
          continue;
        }

        auto isFloorStep = wallA.kind == ArrangementWallKind::FloorStep;
        auto zA = isFloorStep ? wallA.maxZ : wallA.minZ;
        auto zB = isFloorStep ? wallB.maxZ : wallB.minZ;
        if (std::abs(zA - zB) > MinimumChipSize) {
          continue;
        }
        auto z = (zA + zB) * 0.5f;

        // Exactly two wall polygons may touch this 3D point; together with
        // the shared horizontal face they are the requested three polygons.
        auto touchingWalls = std::count_if(
            incident.begin(), incident.end(), [&](IncidentWall const& item) {
              auto const& wall = walls[item.wallIndex];
              return wall.minZ <= z + MinimumChipSize &&
                     wall.maxZ >= z - MinimumChipSize;
            });
        if (touchingWalls != 2) {
          continue;
        }

        auto const parameters =
            wallA.paletteIndex < arrangement.chipParametersPalette.size()
                ? arrangement.chipParametersPalette[wallA.paletteIndex]
                : ChipGenerationParameters{};
        if (parameters.cornerProbability <= 0.0f) {
          continue;
        }
        auto verticalLimit =
            isFloorStep
                ? z - std::max(wallA.minZ, wallB.minZ)
                : std::min(wallA.maxZ, wallB.maxZ) - z;
        auto minimum = parameters.minimumCornerDistance;
        if (minimum > a.length || minimum > b.length ||
            minimum > verticalLimit) {
          continue;
        }

        auto seed = StableCornerSeed(
            arrangement.vertices[vertexIndex], z, wallA.kind);
        if (StableRandom01(seed, 0x5000ull) >=
            parameters.cornerProbability) {
          continue;
        }
        auto chooseDistance = [&](float edgeLength, uint64_t stream) {
          auto maximum =
              std::min(parameters.maximumCornerDistance, edgeLength);
          return minimum + (maximum - minimum) *
                               StableRandom01(seed, stream);
        };
        auto distanceA = chooseDistance(a.length, 0x5001ull);
        auto distanceB = chooseDistance(b.length, 0x5002ull);
        auto verticalDistance = chooseDistance(verticalLimit, 0x5003ull);
        auto pointA = corner + a.ray * distanceA;
        auto pointB = corner + b.ray * distanceB;

        auto setWallCorner = [&](IncidentWall const& item,
                                 float horizontalDistance) {
          auto& notches = wallNotches[item.wallIndex];
          WallCornerCut cut{horizontalDistance, verticalDistance};
          if (isFloorStep) {
            (item.atStart ? notches.topLeft : notches.topRight) = cut;
          } else {
            (item.atStart ? notches.bottomLeft : notches.bottomRight) = cut;
          }
          (item.atStart ? horizontalStartClearance[item.wallIndex]
                        : horizontalEndClearance[item.wallIndex]) =
              horizontalDistance;
        };
        setWallCorner(a, distanceA);
        setWallCorner(b, distanceB);
        verticalCornerClearance[{vertexIndex, std::bit_cast<uint32_t>(z)}] =
            verticalDistance;

        auto faceKind = isFloorStep ? DetailSurfaceKind::FloorOfFace
                                    : DetailSurfaceKind::CeilingOfFace;
        DetailSurfaceKey faceSource{faceKind, *faceA};
        cornerCutsByFace[faceSource].push_back(
            {vertexIndex, wallA.edge, pointA, wallB.edge, pointB});

        auto sourceWall = std::min(a.wallIndex, b.wallIndex);
        DetailSurfaceKey source{DetailSurfaceKind::Wall, sourceWall};
        Vertex3 onA{pointA.x, pointA.y, z};
        Vertex3 onB{pointB.x, pointB.y, z};
        Vertex3 onVertical{
            corner.x, corner.y,
            isFloorStep ? z - verticalDistance : z + verticalDistance};
        Vertex3 reference{
            a.normal.x + b.normal.x, a.normal.y + b.normal.y,
            isFloorStep ? 1.0f : -1.0f};
        std::array<float, 2> uvA{distanceA / a.length, 1.0f};
        std::array<float, 2> uvB{distanceB / b.length, 1.0f};
        std::array<float, 2> uvVertical{
            0.0f,
            isFloorStep ? 1.0f - verticalDistance /
                                     (walls[sourceWall].maxZ -
                                      walls[sourceWall].minZ)
                        : verticalDistance /
                              (walls[sourceWall].maxZ -
                               walls[sourceWall].minZ)};
        AddTriangle(
            detail, source, onA, onB, onVertical, reference, uvA, uvB,
            uvVertical, false, DetailTriangleKind::CornerChipFacet);
        detail.countChip();
      }
    }
  }

  for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
       ++wallIndex) {
    auto const& wall = walls[wallIndex];
    auto const parameters =
        wall.paletteIndex < arrangement.chipParametersPalette.size()
            ? arrangement.chipParametersPalette[wall.paletteIndex]
            : ChipGenerationParameters{};
    if (parameters.probability <= 0.0f) {
      continue;
    }

    // A FloorStep's top Arris and a CeilingStep's bottom are the only convex
    // ones. Invisible walls carry no damage on either adjoining surface.
    auto isFloorStep = wall.kind == ArrangementWallKind::FloorStep;
    if ((!isFloorStep && wall.kind != ArrangementWallKind::CeilingStep) ||
        !wall.visible || !SupportsHorizontalDetail(arrangement, wall)) {
      continue;
    }

    auto const& edge = arrangement.edges[wall.edge];
    auto const& properties0 =
        arrangement.palette[arrangement.faces[edge.face[0]].paletteIndex];
    auto const& properties1 =
        arrangement.palette[arrangement.faces[edge.face[1]].paletteIndex];
    auto bittenFace =
        isFloorStep
            ? (properties0.floorZ.baseElevation >
                       properties1.floorZ.baseElevation
                                 ? edge.face[0]
                                 : edge.face[1])
            : (properties0.ceilingZ.baseElevation <
                       properties1.ceilingZ.baseElevation
                                 ? edge.face[0]
                                 : edge.face[1]);
    if (!arrangement.faces[bittenFace].solid) {
      continue;
    }

    auto orientation = OrientArrangementWall(arrangement, wall);
    auto along = orientation.v1 - orientation.v0;
    auto length = along.length();
    auto direction = along / length;
    auto inward = -orientation.normal;
    auto generated = GenerateChips(
        length, parameters,
        StableArrisSeed(
            arrangement.vertices[edge.v[0]], arrangement.vertices[edge.v[1]]),
        horizontalStartClearance[wallIndex],
        horizontalEndClearance[wallIndex]);

    auto faceKind = isFloorStep ? DetailSurfaceKind::FloorOfFace
                                : DetailSurfaceKind::CeilingOfFace;
    DetailSurfaceKey source{DetailSurfaceKind::Wall, wallIndex};

    for (auto const& chip : generated) {
      auto centreDistance = chip.centre;
      auto midpoint = orientation.v0 + direction * centreDistance;
      auto boundaryLimit = FaceBoundaryDistance(
          arrangement, arrangement.faces[bittenFace], wall.edge, midpoint,
          inward);
      auto depth = std::min(
          {chip.depth, wall.maxZ - wall.minZ, boundaryLimit});
      auto reach = chip.reach;
      if (depth <= MinimumChipSize || reach <= MinimumChipSize) {
        continue;
      }

      auto profile = BuildChipProfile(chip);
      auto notchStart = centreDistance - reach * 0.5f;
      auto notchEnd = centreDistance + reach * 0.5f;
      for (auto const& sample : profile) {
        if (sample.sideA <= 0.0f) {
          continue;
        }
        auto distance = notchStart + sample.t * reach;
        auto origin = orientation.v0 + direction * distance;
        depth = std::min(
            depth,
            FaceBoundaryDistance(
                arrangement, arrangement.faces[bittenFace], wall.edge, origin,
                inward) /
                sample.sideA);
      }
      if (depth <= MinimumChipSize) {
        continue;
      }

      auto arrisZ = isFloorStep ? wall.maxZ : wall.minZ;
      auto verticalSign = isFloorStep ? -1.0f : 1.0f;
      auto height = wall.maxZ - wall.minZ;
      auto arrisV = (arrisZ - wall.minZ) / height;
      std::vector<Vertex3> sideA;
      std::vector<Vertex3> sideB;
      std::vector<std::array<float, 2>> uvSideA;
      std::vector<std::array<float, 2>> uvSideB;
      std::vector<wp::Vector2> horizontalProfile;
      std::vector<WallNotchProfilePoint> wallProfile;
      for (auto const& sample : profile) {
        auto distance = notchStart + sample.t * reach;
        auto onArris = orientation.v0 + direction * distance;
        auto onHorizontal = onArris + inward * (depth * sample.sideA);
        auto wallZ = arrisZ + verticalSign * depth * sample.sideB;
        sideA.push_back({onHorizontal.x, onHorizontal.y, arrisZ});
        sideB.push_back({onArris.x, onArris.y, wallZ});
        uvSideA.push_back({distance / length, arrisV});
        uvSideB.push_back(
            {distance / length, (wallZ - wall.minZ) / height});
        horizontalProfile.push_back(onHorizontal);
        wallProfile.push_back({distance, depth * sample.sideB});
      }
      auto edgeA = orientation.v0 + direction * notchStart;
      auto edgeB = orientation.v0 + direction * notchEnd;
      auto apex = midpoint + inward * depth;
      footprintsByFace[{faceKind, bittenFace}].push_back(
          {wall.edge, edgeA, edgeB, apex, std::move(horizontalProfile)});
      auto& notches = isFloorStep ? wallNotches[wallIndex].top
                                  : wallNotches[wallIndex].bottom;
      notches.push_back(
          {notchStart, notchEnd, depth, std::move(wallProfile)});

      Vertex3 facetReference{
          orientation.normal.x, orientation.normal.y,
          isFloorStep ? 1.0f : -1.0f};
      auto addFacet = [&](Vertex3 const& p0, Vertex3 const& p1,
                          Vertex3 const& p2,
                          std::array<float, 2> const& uv0,
                          std::array<float, 2> const& uv1,
                          std::array<float, 2> const& uv2) {
        AddTriangle(
            detail, source, p0, p1, p2, facetReference, uv0, uv1, uv2,
            false, DetailTriangleKind::HorizontalChipFacet, chip.type);
      };
      if (chip.type == ChipType::PyramidalDivot) {
        Vertex3 deepest{
            apex.x, apex.y, arrisZ + verticalSign * depth};
        std::array<float, 2> uvDeepest{
            centreDistance / length,
            (deepest.z - wall.minZ) / height};
        std::vector<Vertex3> perimeter = sideA;
        std::vector<std::array<float, 2>> perimeterUv = uvSideA;
        for (size_t i = sideB.size(); i-- > 0;) {
          perimeter.push_back(sideB[i]);
          perimeterUv.push_back(uvSideB[i]);
        }
        for (size_t i = 0; i < perimeter.size(); ++i) {
          auto next = (i + 1) % perimeter.size();
          addFacet(
              perimeter[i], perimeter[next], deepest, perimeterUv[i],
              perimeterUv[next], uvDeepest);
        }
      } else if (chip.type == ChipType::VShapedNotch) {
        std::vector<Vertex3> crease;
        std::vector<std::array<float, 2>> uvCrease;
        for (size_t i = 0; i < profile.size(); ++i) {
          auto const& sample = profile[i];
          auto distance = notchStart + sample.t * reach;
          auto onArris = orientation.v0 + direction * distance;
          auto point = onArris + inward * (depth * sample.sideA / 0.65f);
          crease.push_back(
              {point.x, point.y,
               arrisZ + verticalSign * depth * sample.sideB / 0.65f});
          uvCrease.push_back(
              {distance / length,
               (crease.back().z - wall.minZ) / height});
        }
        for (size_t i = 0; i + 1 < profile.size(); ++i) {
          addFacet(
              sideA[i], sideA[i + 1], crease[i + 1], uvSideA[i],
              uvSideA[i + 1], uvCrease[i + 1]);
          addFacet(
              sideA[i], crease[i + 1], crease[i], uvSideA[i],
              uvCrease[i + 1], uvCrease[i]);
          addFacet(
              crease[i], crease[i + 1], sideB[i + 1], uvCrease[i],
              uvCrease[i + 1], uvSideB[i + 1]);
          addFacet(
              crease[i], sideB[i + 1], sideB[i], uvCrease[i],
              uvSideB[i + 1], uvSideB[i]);
        }
      } else {
        for (size_t i = 0; i + 1 < profile.size(); ++i) {
          addFacet(
              sideA[i], sideA[i + 1], sideB[i + 1], uvSideA[i],
              uvSideA[i + 1], uvSideB[i + 1]);
          addFacet(
              sideA[i], sideB[i + 1], sideB[i], uvSideA[i],
              uvSideB[i + 1], uvSideB[i]);
        }
      }
      detail.countChip();
    }
  }

  // Vertical Arrises: pairs of visible walls meeting in a concave angle as
  // seen from their common front (navigable) side. Both walls must use the
  // same Sub-material, leaving one unambiguous material/configuration.
  for (auto const& [vertexIndex, incident] : wallsByVertex) {
    auto corner = ToWorld(arrangement.vertices[vertexIndex]);
    for (size_t aIndex = 0; aIndex < incident.size(); ++aIndex) {
      for (size_t bIndex = aIndex + 1; bIndex < incident.size(); ++bIndex) {
        auto const* aPtr = &incident[aIndex];
        auto const* bPtr = &incident[bIndex];
        if (bPtr->ray.x < aPtr->ray.x ||
            (bPtr->ray.x == aPtr->ray.x && bPtr->ray.y < aPtr->ray.y)) {
          std::swap(aPtr, bPtr);
        }
        auto const& a = *aPtr;
        auto const& b = *bPtr;
        auto const& wallA = walls[a.wallIndex];
        auto const& wallB = walls[b.wallIndex];
        auto const& propertiesA = arrangement.palette[wallA.paletteIndex];
        auto const& propertiesB = arrangement.palette[wallB.paletteIndex];
        if (!SupportsWallDetail(arrangement, wallA) ||
            !SupportsWallDetail(arrangement, wallB) ||
            propertiesA.wallMaterialId != propertiesB.wallMaterialId) {
          continue;
        }
        if (!IsEligibleVerticalAngle(a, b)) {
          continue;
        }

        auto minZ = std::max(wallA.minZ, wallB.minZ);
        auto maxZ = std::min(wallA.maxZ, wallB.maxZ);
        bool exactlyTwoWalls = true;
        for (size_t otherIndex = 0; otherIndex < incident.size();
             ++otherIndex) {
          if (otherIndex == aIndex || otherIndex == bIndex) {
            continue;
          }
          auto const& other = walls[incident[otherIndex].wallIndex];
          if (std::min(maxZ, other.maxZ) - std::max(minZ, other.minZ) >
              MinimumChipSize) {
            exactlyTwoWalls = false;
            break;
          }
        }
        if (!exactlyTwoWalls) {
          continue;
        }
        auto const parameters =
            wallA.paletteIndex < arrangement.chipParametersPalette.size()
                ? arrangement.chipParametersPalette[wallA.paletteIndex]
                : ChipGenerationParameters{};
        auto clearanceAt = [&](float z) {
          auto found = verticalCornerClearance.find(
              {vertexIndex, std::bit_cast<uint32_t>(z)});
          return found == verticalCornerClearance.end() ? 0.0f
                                                        : found->second;
        };
        auto generated = GenerateChips(
            maxZ - minZ, parameters,
            StableVerticalArrisSeed(
                arrangement.vertices[vertexIndex], minZ, maxZ),
            clearanceAt(minZ), clearanceAt(maxZ));
        for (auto const& chip : generated) {
          auto depth = std::min({chip.depth, a.length, b.length});
          if (depth <= MinimumChipSize || chip.reach <= MinimumChipSize) {
            continue;
          }
          auto centreZ = minZ + chip.centre;
          auto bottomZ = centreZ - chip.reach * 0.5f;
          auto topZ = centreZ + chip.reach * 0.5f;
          auto profile = BuildChipProfile(chip);
          WallNotch proposedA{bottomZ, topZ, depth};
          WallNotch proposedB{bottomZ, topZ, depth};
          for (auto const& sample : profile) {
            auto z = bottomZ + sample.t * chip.reach;
            proposedA.profile.push_back({z, depth * sample.sideA});
            proposedB.profile.push_back({z, depth * sample.sideB});
          }
          if (!VerticalNotchFits(
                  wallNotches[a.wallIndex], a.atStart, proposedA, a.length,
                  wallA.minZ, wallA.maxZ) ||
              !VerticalNotchFits(
                  wallNotches[b.wallIndex], b.atStart, proposedB, b.length,
                  wallB.minZ, wallB.maxZ)) {
            continue;
          }

          auto addWallNotch = [&](IncidentWall const& incidentWall,
                                  WallNotch notch) {
            auto& notches = wallNotches[incidentWall.wallIndex];
            auto& side = incidentWall.atStart ? notches.left : notches.right;
            side.push_back(std::move(notch));
          };
          addWallNotch(a, std::move(proposedA));
          addWallNotch(b, std::move(proposedB));

          auto sourceWall = std::min(a.wallIndex, b.wallIndex);
          DetailSurfaceKey source{DetailSurfaceKind::Wall, sourceWall};
          // Canonical wall normals point out of the wall material (a Border's
          // point toward its polygon), so their opposite bisector goes into
          // the material and keeps every profile centred on the shared edge.
          auto intoMaterial = -(a.normal + b.normal).normalisedCopy();
          Vertex3 facetReference{
              -intoMaterial.x, -intoMaterial.y, 0.0f};
          auto const& sourceWallData = walls[sourceWall];
          auto sourceHeight = sourceWallData.maxZ - sourceWallData.minZ;
          std::vector<Vertex3> sideA;
          std::vector<Vertex3> sideB;
          std::vector<Vertex3> crease;
          std::vector<std::array<float, 2>> uvA;
          std::vector<std::array<float, 2>> uvB;
          std::vector<std::array<float, 2>> uvCrease;
          for (auto const& sample : profile) {
            auto z = bottomZ + sample.t * chip.reach;
            auto pointA = corner + a.ray * (depth * sample.sideA);
            auto pointB = corner + b.ray * (depth * sample.sideB);
            auto creaseFactor = std::max(sample.sideA, sample.sideB);
            if (chip.type == ChipType::VShapedNotch) {
              creaseFactor /= 0.65f;
            }
            auto centre = corner + intoMaterial * (depth * creaseFactor);
            sideA.push_back({pointA.x, pointA.y, z});
            sideB.push_back({pointB.x, pointB.y, z});
            crease.push_back({centre.x, centre.y, z});
            auto v = (z - sourceWallData.minZ) / sourceHeight;
            uvA.push_back({depth * sample.sideA / a.length, v});
            uvB.push_back({depth * sample.sideB / b.length, v});
            uvCrease.push_back(
                {(uvA.back()[0] + uvB.back()[0]) * 0.5f, v});
          }
          auto addFacet = [&](Vertex3 const& p0, Vertex3 const& p1,
                              Vertex3 const& p2,
                              std::array<float, 2> const& uv0,
                              std::array<float, 2> const& uv1,
                              std::array<float, 2> const& uv2) {
            AddTriangle(
                detail, source, p0, p1, p2, facetReference, uv0, uv1, uv2,
                false, DetailTriangleKind::VerticalChipFacet, chip.type);
          };
          if (chip.type == ChipType::PyramidalDivot) {
            auto centreSample = profile.size() / 2;
            auto deepestPlan = corner + intoMaterial * depth;
            Vertex3 deepest{deepestPlan.x, deepestPlan.y, centreZ};
            auto deepestUv = uvCrease[centreSample];
            std::vector<Vertex3> perimeter = sideA;
            std::vector<std::array<float, 2>> perimeterUv = uvA;
            for (size_t i = sideB.size(); i-- > 0;) {
              perimeter.push_back(sideB[i]);
              perimeterUv.push_back(uvB[i]);
            }
            for (size_t i = 0; i < perimeter.size(); ++i) {
              auto next = (i + 1) % perimeter.size();
              addFacet(
                  perimeter[i], perimeter[next], deepest, perimeterUv[i],
                  perimeterUv[next], deepestUv);
            }
          } else {
            for (size_t i = 0; i + 1 < profile.size(); ++i) {
              addFacet(
                  sideA[i], sideA[i + 1], crease[i + 1], uvA[i], uvA[i + 1],
                  uvCrease[i + 1]);
              addFacet(
                  sideA[i], crease[i + 1], crease[i], uvA[i],
                  uvCrease[i + 1], uvCrease[i]);
              addFacet(
                  crease[i], crease[i + 1], sideB[i + 1], uvCrease[i],
                  uvCrease[i + 1], uvB[i + 1]);
              addFacet(
                  crease[i], sideB[i + 1], sideB[i], uvCrease[i], uvB[i + 1],
                  uvB[i]);
            }
          }
          detail.countChip();
        }
      }
    }
  }

  // Rebuild every affected wall once, even when several horizontal and
  // vertical Arrises have bitten into it.
  for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
       ++wallIndex) {
    auto const& notches = wallNotches[wallIndex];
    if (notches.empty()) {
      continue;
    }
    DetailSurfaceKey source{DetailSurfaceKind::Wall, wallIndex};
    detail.addSuppressed(source);
    auto orientation = OrientArrangementWall(arrangement, walls[wallIndex]);
    auto along = orientation.v1 - orientation.v0;
    auto length = along.length();
    Vertex3 wallReference{orientation.normal.x, orientation.normal.y, 0.0f};
    AddWallRemainder(
        detail, source, orientation.v0, along / length, wallReference, length,
        walls[wallIndex].minZ, walls[wallIndex].maxZ, notches);
  }

  for (auto const& [key, footprints] : footprintsByFace) {
    AddRebuiltFaceHorizontal(
        detail, arrangement, key, footprints, cornerCutsByFace[key]);
  }
  for (auto const& [key, cornerCuts] : cornerCutsByFace) {
    if (footprintsByFace.contains(key)) {
      continue;
    }
    AddRebuiltFaceHorizontal(detail, arrangement, key, {}, cornerCuts);
  }

  // Wedges are additive: unlike Chips they suppress neither attachment
  // surface. Each eligible Border wall contributes two three-triangle fans
  // around a subtly convex central line arched toward the wall, routed
  // through the adjoining ceiling or floor face.
  if (wedgeParameters.enabled) {
    static std::vector<Footprint> const noFootprints;
    static std::vector<FaceCornerCut> const noCornerCuts;
    for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
         ++wallIndex) {
      auto const& wall = walls[wallIndex];
      if (!wall.visible || wall.kind != ArrangementWallKind::Border ||
          !SupportsWallDetail(arrangement, wall)) {
        continue;
      }
      auto const& edge = arrangement.edges[wall.edge];
      auto solidFace = arrangement.faces[edge.face[0]].solid
                           ? edge.face[0]
                           : edge.face[1];
      auto orientation = OrientArrangementWall(arrangement, wall);
      auto along = orientation.v1 - orientation.v0;
      auto availableReach = along.length();
      auto availableHeight = wall.maxZ - wall.minZ;
      if (availableReach + FootprintEpsilon <
              wedgeParameters.minimumReach ||
          availableHeight + FootprintEpsilon <
              wedgeParameters.minimumDropDownHeight) {
        continue;
      }
      auto direction = along / availableReach;
      auto wallReservations = WallChipReservations(
          wallNotches[wallIndex], availableReach, wall.minZ, wall.maxZ);
      auto seed = StableArrisSeed(
          arrangement.vertices[edge.v[0]], arrangement.vertices[edge.v[1]]);

      auto addWedge =
          [&](bool atTop, float centreDistance, uint32_t candidateIndex) {
        auto midpoint = orientation.v0 + direction * centreDistance;
        auto centredReach =
            2.0f * std::min(centreDistance, availableReach - centreDistance);
        auto centreRayDepth = FaceBoundaryDistance(
            arrangement, arrangement.faces[solidFace], wall.edge, midpoint,
            orientation.normal);
        if (centreRayDepth + FootprintEpsilon <
            wedgeParameters.minimumProjectionDepth) {
          return;
        }
        auto footprint = [&](float reach, float depth) {
          return std::array<wp::Vector2, 3>{
              midpoint - direction * (reach * 0.5f),
              midpoint + direction * (reach * 0.5f),
              midpoint + orientation.normal * depth};
        };
        auto candidateSeed = Mix(
            seed ^ Mix((atTop ? 0xf1000000ull : 0xf2000000ull) +
                       candidateIndex));
        DetailSurfaceKey source{
            atTop ? DetailSurfaceKind::CeilingOfFace
                  : DetailSurfaceKind::FloorOfFace,
            solidFace};
        auto footprintEntry = footprintsByFace.find(source);
        auto const& chipFootprints =
            footprintEntry == footprintsByFace.end() ? noFootprints
                                                     : footprintEntry->second;
        auto cornerEntry = cornerCutsByFace.find(source);
        auto const& chipCornerCuts =
            cornerEntry == cornerCutsByFace.end() ? noCornerCuts
                                                 : cornerEntry->second;
        auto horizontalFits = [&](float reach, float depth) {
          auto candidate = footprint(reach, depth);
          return WedgeFootprintFits(
                     arrangement, arrangement.faces[solidFace], candidate) &&
                 WedgeHorizontalSurfaceAvoidsChips(
                     arrangement, wall.edge, candidate, chipFootprints,
                     chipCornerCuts);
        };
        auto wallFits = [&](float reach, float verticalExtent) {
          return WedgeWallAvoidsChips(
              centreDistance, reach, verticalExtent, availableReach,
              availableHeight, atTop, wallNotches[wallIndex], wallReservations);
        };
        auto reachMaximum = std::min(
            wedgeParameters.maximumReach, centredReach);
        auto fittedReachMaximum = CapMonotonicRange(
            wedgeParameters.minimumReach, reachMaximum, [&](float reach) {
              return horizontalFits(
                         reach, wedgeParameters.minimumProjectionDepth) &&
                     wallFits(
                         reach, wedgeParameters.minimumDropDownHeight);
            });
        if (!fittedReachMaximum) return;

        // Use the widest fitted footprint when capping the other dimensions,
        // so every independently selected combination remains valid.
        auto depthMaximum = std::min(
            wedgeParameters.maximumProjectionDepth, centreRayDepth);
        auto fittedDepthMaximum = CapMonotonicRange(
            wedgeParameters.minimumProjectionDepth, depthMaximum,
            [&](float depth) {
              return horizontalFits(*fittedReachMaximum, depth);
            });
        if (!fittedDepthMaximum) return;

        auto verticalMaximum = std::min(
            wedgeParameters.maximumDropDownHeight, availableHeight);
        auto fittedVerticalMaximum = CapMonotonicRange(
            wedgeParameters.minimumDropDownHeight, verticalMaximum,
            [&](float extent) {
              return wallFits(*fittedReachMaximum, extent);
            });
        if (!fittedVerticalMaximum) return;

        auto draw = [&](float minimum, float maximum, uint64_t stream) {
          return minimum +
                 (maximum - minimum) * StableRandom01(candidateSeed, stream);
        };
        auto reach = draw(
            wedgeParameters.minimumReach, *fittedReachMaximum, 0x7001ull);
        auto verticalExtent = draw(
            wedgeParameters.minimumDropDownHeight, *fittedVerticalMaximum,
            0x7002ull);
        auto depth = draw(
            wedgeParameters.minimumProjectionDepth, *fittedDepthMaximum,
            0x7003ull);

        auto endpointA = midpoint - direction * (reach * 0.5f);
        auto endpointB = midpoint + direction * (reach * 0.5f);
        auto projected = midpoint + orientation.normal * depth;
        auto arrisZ = atTop ? wall.maxZ : wall.minZ;
        auto wallPointZ =
            arrisZ + (atTop ? -verticalExtent : verticalExtent);
        Vertex3 a{endpointA.x, endpointA.y, arrisZ};
        Vertex3 b{endpointB.x, endpointB.y, arrisZ};
        Vertex3 c{projected.x, projected.y, arrisZ};
        Vertex3 d{midpoint.x, midpoint.y, wallPointZ};
        Vertex3 attachment{midpoint.x, midpoint.y, arrisZ};
        auto centralPoint = [&](float t) {
          Vertex3 straight{
              c.x + (d.x - c.x) * t,
              c.y + (d.y - c.y) * t,
              c.z + (d.z - c.z) * t};
          auto offset =
              WedgeCentralLineConvexity * 4.0f * t * (1.0f - t);
          return Vertex3{
              straight.x + (attachment.x - straight.x) * offset,
              straight.y + (attachment.y - straight.y) * offset,
              straight.z};
        };
        std::array<Vertex3, 4> central{
            c, centralPoint(1.0f / 3.0f), centralPoint(2.0f / 3.0f), d};
        auto horizontalUv = [](Vertex3 const& vertex) {
          return std::array<float, 2>{
              vertex.x / HorizontalUvScale, vertex.y / HorizontalUvScale};
        };
        auto verticalNormal = atTop ? -1.0f : 1.0f;
        Vertex3 referenceA{
            -direction.x + orientation.normal.x,
            -direction.y + orientation.normal.y, verticalNormal};
        Vertex3 referenceB{
            direction.x + orientation.normal.x,
            direction.y + orientation.normal.y, verticalNormal};
        for (size_t segment = 0; segment + 1 < central.size(); ++segment) {
          AddWedgeTriangle(
              detail, source, a, central[segment], central[segment + 1],
              referenceA, horizontalUv(a), horizontalUv(central[segment]),
              horizontalUv(central[segment + 1]), wedgeParameters.quality,
              candidateSeed, 0x7100ull + segment * 2);
          AddWedgeTriangle(
              detail, source, central[segment], b, central[segment + 1],
              referenceB, horizontalUv(central[segment]), horizontalUv(b),
              horizontalUv(central[segment + 1]), wedgeParameters.quality,
              candidateSeed, 0x7101ull + segment * 2);
        }
        detail.countWedge();
      };

      auto ceilingCentres = GenerateWedgeCentres(
          availableReach, wedgeParameters.minimumReach,
          wedgeParameters.ceilingWedgesPerUnitDistance, seed, 0xf100ull);
      for (uint32_t candidate = 0;
           candidate < uint32_t(ceilingCentres.size()); ++candidate) {
        addWedge(true, ceilingCentres[candidate], candidate);
      }
      auto floorCentres = GenerateWedgeCentres(
          availableReach, wedgeParameters.minimumReach,
          wedgeParameters.floorWedgesPerUnitDistance, seed, 0xf200ull);
      for (uint32_t candidate = 0;
           candidate < uint32_t(floorCentres.size()); ++candidate) {
        addWedge(false, floorCentres[candidate], candidate);
      }
    }

    // A Corner Wedge fills the trihedral meeting of one horizontal surface
    // and two connected visible Border walls. Its hidden attachment faces lie
    // on those three source surfaces; only the triangular exposed face is
    // emitted. Concave/hole corners naturally fail the complete footprint
    // test rather than projecting into unavailable space.
    for (auto const& [vertexIndex, incident] : wallsByVertex) {
      auto corner = ToWorld(arrangement.vertices[vertexIndex]);
      for (size_t aIndex = 0; aIndex < incident.size(); ++aIndex) {
        for (size_t bIndex = aIndex + 1; bIndex < incident.size(); ++bIndex) {
          auto const* aPtr = &incident[aIndex];
          auto const* bPtr = &incident[bIndex];
          if (bPtr->ray.x < aPtr->ray.x ||
              (bPtr->ray.x == aPtr->ray.x && bPtr->ray.y < aPtr->ray.y)) {
            std::swap(aPtr, bPtr);
          }
          auto const& a = *aPtr;
          auto const& b = *bPtr;
          auto const& wallA = walls[a.wallIndex];
          auto const& wallB = walls[b.wallIndex];
          if (wallA.kind != ArrangementWallKind::Border ||
              wallB.kind != ArrangementWallKind::Border ||
              !SupportsWallDetail(arrangement, wallA) ||
              !SupportsWallDetail(arrangement, wallB) ||
              std::abs(a.ray.x * b.ray.y - a.ray.y * b.ray.x) <=
                  ConcavityEpsilon) {
            continue;
          }
          auto solidFaceFor = [&](ArrangementWall const& wall) {
            auto const& edge = arrangement.edges[wall.edge];
            return arrangement.faces[edge.face[0]].solid ? edge.face[0]
                                                         : edge.face[1];
          };
          auto solidFace = solidFaceFor(wallA);
          if (solidFaceFor(wallB) != solidFace) continue;
          auto const& edgeA = arrangement.edges[wallA.edge];
          auto const& edgeB = arrangement.edges[wallB.edge];
          auto wallReservationsA = WallChipReservations(
              wallNotches[a.wallIndex], a.length, wallA.minZ, wallA.maxZ);
          auto wallReservationsB = WallChipReservations(
              wallNotches[b.wallIndex], b.length, wallB.minZ, wallB.maxZ);
          auto availableHeight = std::min(
              wallA.maxZ - wallA.minZ, wallB.maxZ - wallB.minZ);
          if (a.length + FootprintEpsilon <
                  wedgeParameters.minimumCornerReach ||
              b.length + FootprintEpsilon <
                  wedgeParameters.minimumCornerReach ||
              availableHeight + FootprintEpsilon <
                  wedgeParameters.minimumCornerVerticalExtent) {
            continue;
          }

          auto addCornerWedge = [&](bool atTop) {
            auto arrisZ = atTop ? wallA.maxZ : wallA.minZ;
            auto seed = StableCornerSeed(
                arrangement.vertices[vertexIndex], arrisZ,
                ArrangementWallKind::Border);
            if (StableRandom01(seed, 0x8000ull) >=
                wedgeParameters.cornerWedgeProbability) {
              return;
            }
            DetailSurfaceKey source{
                atTop ? DetailSurfaceKind::CeilingOfFace
                      : DetailSurfaceKind::FloorOfFace,
                solidFace};
            auto footprintEntry = footprintsByFace.find(source);
            auto const& chipFootprints =
                footprintEntry == footprintsByFace.end() ? noFootprints
                                                         : footprintEntry->second;
            auto cornerEntry = cornerCutsByFace.find(source);
            auto const& chipCornerCuts =
                cornerEntry == cornerCutsByFace.end() ? noCornerCuts
                                                     : cornerEntry->second;
            auto footprint = [&](float reachA, float reachB) {
              return std::array<wp::Vector2, 3>{
                  corner, corner + a.ray * reachA,
                  corner + b.ray * reachB};
            };
            auto horizontalFits = [&](float reachA, float reachB) {
              auto candidate = footprint(reachA, reachB);
              return WedgeFootprintFits(
                         arrangement, arrangement.faces[solidFace], candidate) &&
                     CornerWedgeHorizontalSurfaceAvoidsChips(
                         arrangement, wallA.edge, wallB.edge, candidate,
                         chipFootprints, chipCornerCuts);
            };
            auto wallFits = [&](IncidentWall const& item,
                                ArrangementWall const& wall,
                                float reach,
                                float verticalExtent,
                                auto const& reservations) {
              auto length = item.length;
              auto height = wall.maxZ - wall.minZ;
              auto cornerX = item.atStart ? 0.0f : length;
              auto reachX = item.atStart ? reach : length - reach;
              auto arrisY = atTop ? 0.0f : height;
              auto verticalY =
                  atTop ? verticalExtent : height - verticalExtent;
              std::array<wp::Vector2, 3> attachment{
                  wp::Vector2{cornerX, arrisY},
                  wp::Vector2{reachX, arrisY},
                  wp::Vector2{cornerX, verticalY}};
              return WedgeWallAttachmentAvoidsChips(
                  cornerX, reachX, attachment, length, atTop,
                  wallNotches[item.wallIndex], reservations);
            };

            auto maximumA =
                std::min(wedgeParameters.maximumCornerReach, a.length);
            auto fittedA = CapMonotonicRange(
                wedgeParameters.minimumCornerReach, maximumA,
                [&](float reachA) {
                  return horizontalFits(
                             reachA, wedgeParameters.minimumCornerReach) &&
                         wallFits(
                             a, wallA, reachA,
                             wedgeParameters.minimumCornerVerticalExtent,
                             wallReservationsA);
                });
            if (!fittedA) return;
            auto maximumB =
                std::min(wedgeParameters.maximumCornerReach, b.length);
            auto fittedB = CapMonotonicRange(
                wedgeParameters.minimumCornerReach, maximumB,
                [&](float reachB) {
                  return horizontalFits(*fittedA, reachB) &&
                         wallFits(
                             b, wallB, reachB,
                             wedgeParameters.minimumCornerVerticalExtent,
                             wallReservationsB);
                });
            if (!fittedB) return;
            auto verticalMaximum = std::min(
                wedgeParameters.maximumCornerVerticalExtent,
                availableHeight);
            auto fittedVertical = CapMonotonicRange(
                wedgeParameters.minimumCornerVerticalExtent,
                verticalMaximum, [&](float extent) {
                  return wallFits(
                             a, wallA, *fittedA, extent,
                             wallReservationsA) &&
                         wallFits(
                             b, wallB, *fittedB, extent,
                             wallReservationsB);
                });
            if (!fittedVertical) return;

            auto draw = [&](float minimum, float maximum, uint64_t stream) {
              return minimum +
                     (maximum - minimum) * StableRandom01(seed, stream);
            };
            auto reachA = draw(
                wedgeParameters.minimumCornerReach, *fittedA, 0x8001ull);
            auto reachB = draw(
                wedgeParameters.minimumCornerReach, *fittedB, 0x8002ull);
            auto verticalExtent = draw(
                wedgeParameters.minimumCornerVerticalExtent,
                *fittedVertical, 0x8003ull);
            auto pointA = corner + a.ray * reachA;
            auto pointB = corner + b.ray * reachB;
            auto verticalZ =
                arrisZ + (atTop ? -verticalExtent : verticalExtent);
            Vertex3 exposedA{pointA.x, pointA.y, arrisZ};
            Vertex3 exposedB{pointB.x, pointB.y, arrisZ};
            Vertex3 exposedVertical{corner.x, corner.y, verticalZ};
            Vertex3 reference{
                a.normal.x + b.normal.x,
                a.normal.y + b.normal.y,
                atTop ? -1.0f : 1.0f};
            auto horizontalUv = [](Vertex3 const& vertex) {
              return std::array<float, 2>{
                  vertex.x / HorizontalUvScale,
                  vertex.y / HorizontalUvScale};
            };
            AddWedgeTriangle(
                detail, source, exposedA, exposedB, exposedVertical,
                reference, horizontalUv(exposedA), horizontalUv(exposedB),
                horizontalUv(exposedVertical), wedgeParameters.quality, seed,
                0x8100ull);
            detail.countWedge();
          };

          addCornerWedge(true);
          addCornerWedge(false);
        }
      }
    }
  }

  detail.sort();
  return detail;
}
}  // namespace bw::core::arr
