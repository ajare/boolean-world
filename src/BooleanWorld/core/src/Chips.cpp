#include "core/Chips.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
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

struct GeneratedChip {
  float centre;
  float depth;
  float reach;
};

std::vector<GeneratedChip> GenerateChips(
    float length,
    ChipGenerationParameters const& parameters,
    uint64_t seed) {
  std::vector<GeneratedChip> chips;
  if (parameters.probability <= 0.0f ||
      length < parameters.minimumArrisLength || length <= MinimumChipSize) {
    return chips;
  }

  auto authoredMaximumReach =
      std::min(parameters.maximumReach, MaximumChipReach);
  auto authoredMinimumReach =
      std::min(parameters.minimumReach, authoredMaximumReach);
  auto maximumReach = std::min(authoredMaximumReach, length);
  auto minimumSpacing =
      std::max(parameters.minimumSpacing, authoredMaximumReach + 0.1f);
  auto usableForCentres = std::max(0.0f, length - maximumReach);
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
    auto centre = maximumReach * 0.5f + float(chip) * minimumSpacing +
                  offsets[chip] * slack;
    auto depth = parameters.minimumDepth +
                 (parameters.maximumDepth - parameters.minimumDepth) *
                     StableRandom01(seed, 0x3000ull + chip);
    auto reach = authoredMinimumReach +
                 (authoredMaximumReach - authoredMinimumReach) *
                     StableRandom01(seed, 0x4000ull + chip);
    chips.push_back({centre, depth, std::min(reach, length)});
  }
  return chips;
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
    DetailTriangleKind kind = DetailTriangleKind::SurfaceRemainder) {
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
  detail.addTriangle({source, {a, b, c}, kind, followsWallFacing});
}

// One Chip's footprint on the horizontal face it bit into: the two points
// where it meets the Arris, and the apex it reaches to inside the face.
struct Footprint {
  uint32_t edgeIndex;
  wp::Vector2 a;
  wp::Vector2 b;
  wp::Vector2 apex;
};

// One triangular notch in a wall boundary. start/end run in increasing local
// x for horizontal boundaries and increasing z for vertical boundaries.
struct WallNotch {
  float start;
  float end;
  float depth;
};

struct WallNotches {
  std::vector<WallNotch> bottom;
  std::vector<WallNotch> right;
  std::vector<WallNotch> top;
  std::vector<WallNotch> left;

  [[nodiscard]] bool empty() const {
    return bottom.empty() && right.empty() && top.empty() && left.empty();
  }
};

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
  push(0.0f, minZ);
  for (auto const& notch : notches.bottom) {
    push(notch.start, minZ);
    push((notch.start + notch.end) * 0.5f, minZ + notch.depth);
    push(notch.end, minZ);
  }
  push(length, minZ);
  for (auto const& notch : notches.right) {
    push(length, notch.start);
    push(length - notch.depth, (notch.start + notch.end) * 0.5f);
    push(length, notch.end);
  }
  push(length, maxZ);
  for (auto it = notches.top.rbegin(); it != notches.top.rend(); ++it) {
    push(it->end, maxZ);
    push((it->start + it->end) * 0.5f, maxZ - it->depth);
    push(it->start, maxZ);
  }
  push(0.0f, maxZ);
  for (auto it = notches.left.rbegin(); it != notches.left.rend(); ++it) {
    push(0.0f, it->end);
    push(it->depth, (it->start + it->end) * 0.5f);
    push(0.0f, it->start);
  }

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

// Rebuilds one horizontal face's floor or ceiling with every Chip footprint
// on it subtracted from its boundary polygon, rather than clipping the
// individual triangles the unchipped face earcut to.
void AddRebuiltFaceHorizontal(
    DetailGeometry& detail,
    ArrangementResult const& arrangement,
    DetailSurfaceKey const& source,
    std::vector<Footprint> const& footprints) {
  using EarcutPoint = std::array<double, 2>;

  auto const& face = arrangement.faces[source.index];
  auto isFloor = source.kind == DetailSurfaceKind::FloorOfFace;
  auto const& properties = arrangement.palette[face.paletteIndex];
  auto z = isFloor ? properties.floorZ : properties.ceilingZ;

  std::vector<std::vector<EarcutPoint>> polygons;
  // Parallel to earcut's own index space, which runs across every ring in
  // the order they are added.
  std::vector<wp::Vector2> positions;

  auto addBoundary = [&](std::vector<uint32_t> const& boundary,
                         std::vector<uint32_t> const& boundaryVertices) {
    std::vector<EarcutPoint> polygon;
    auto push = [&](wp::Vector2 const& point) {
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
      auto current = ToWorld(arrangement.vertices[boundaryVertices[i]]);
      auto next = ToWorld(
          arrangement.vertices[boundaryVertices[(i + 1) % count]]);
      push(current);
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
        auto const& first = aFirst ? footprint->a : footprint->b;
        auto const& second = aFirst ? footprint->b : footprint->a;
        pushDistinct(first, current, next);
        push(footprint->apex);
        pushDistinct(second, current, next);
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

DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls) {
  DetailGeometry detail;
  std::map<DetailSurfaceKey, std::vector<Footprint>> footprintsByFace;
  std::vector<WallNotches> wallNotches(walls.size());

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
        !wall.visible) {
      continue;
    }

    auto const& edge = arrangement.edges[wall.edge];
    auto const& properties0 =
        arrangement.palette[arrangement.faces[edge.face[0]].paletteIndex];
    auto const& properties1 =
        arrangement.palette[arrangement.faces[edge.face[1]].paletteIndex];
    auto bittenFace = isFloorStep
                          ? (properties0.floorZ > properties1.floorZ
                                 ? edge.face[0]
                                 : edge.face[1])
                          : (properties0.ceilingZ < properties1.ceilingZ
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
            arrangement.vertices[edge.v[0]], arrangement.vertices[edge.v[1]]));

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

      auto half = direction * (reach * 0.5f);
      auto a = midpoint - half;
      auto b = midpoint + half;
      auto apex = midpoint + inward * depth;
      footprintsByFace[{faceKind, bittenFace}].push_back(
          {wall.edge, a, b, apex});

      auto notchStart = centreDistance - reach * 0.5f;
      auto notchEnd = centreDistance + reach * 0.5f;
      auto& notches = isFloorStep ? wallNotches[wallIndex].top
                                  : wallNotches[wallIndex].bottom;
      notches.push_back({notchStart, notchEnd, depth});

      auto arrisZ = isFloorStep ? wall.maxZ : wall.minZ;
      auto wallZAtDepth =
          isFloorStep ? wall.maxZ - depth : wall.minZ + depth;
      Vertex3 onArrisA{a.x, a.y, arrisZ};
      Vertex3 onArrisB{b.x, b.y, arrisZ};
      Vertex3 onHorizontal{apex.x, apex.y, arrisZ};
      Vertex3 onWall{midpoint.x, midpoint.y, wallZAtDepth};
      Vertex3 facetReference{
          orientation.normal.x, orientation.normal.y,
          isFloorStep ? 1.0f : -1.0f};
      auto height = wall.maxZ - wall.minZ;
      auto arrisV = (arrisZ - wall.minZ) / height;
      auto wallV = (wallZAtDepth - wall.minZ) / height;
      std::array<float, 2> uvA{notchStart / length, arrisV};
      std::array<float, 2> uvB{notchEnd / length, arrisV};
      std::array<float, 2> uvHorizontal{centreDistance / length, arrisV};
      std::array<float, 2> uvWall{centreDistance / length, wallV};
      AddTriangle(
          detail, source, onArrisA, onHorizontal, onWall, facetReference, uvA,
          uvHorizontal, uvWall, false,
          DetailTriangleKind::HorizontalChipFacet);
      AddTriangle(
          detail, source, onHorizontal, onArrisB, onWall, facetReference,
          uvHorizontal, uvB, uvWall, false,
          DetailTriangleKind::HorizontalChipFacet);
      detail.countChip();
    }
  }

  // Vertical Arrises: pairs of visible walls meeting in a concave angle as
  // seen from their common front (navigable) side. Both walls must use the
  // same Sub-material, leaving one unambiguous material/configuration.
  struct IncidentWall {
    uint32_t wallIndex;
    bool atStart;
    wp::Vector2 ray;
    wp::Vector2 normal;
    float length;
  };
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
    // OrientArrangementWall already gives every wall its canonical front
    // normal. In particular, a Border wall's normal faces toward its polygon.
    wallsByVertex[startVertex].push_back(
        {wallIndex, true, direction, orientation.normal, length});
    wallsByVertex[endVertex].push_back(
        {wallIndex, false, -direction, orientation.normal, length});
  }

  for (auto const& [vertexIndex, incident] : wallsByVertex) {
    auto corner = ToWorld(arrangement.vertices[vertexIndex]);
    for (size_t aIndex = 0; aIndex < incident.size(); ++aIndex) {
      for (size_t bIndex = aIndex + 1; bIndex < incident.size(); ++bIndex) {
        auto const& a = incident[aIndex];
        auto const& b = incident[bIndex];
        auto const& wallA = walls[a.wallIndex];
        auto const& wallB = walls[b.wallIndex];
        auto const& propertiesA = arrangement.palette[wallA.paletteIndex];
        auto const& propertiesB = arrangement.palette[wallB.paletteIndex];
        if (propertiesA.wallMaterialId != propertiesB.wallMaterialId) {
          continue;
        }
        // Measure the angle on the canonical front side of both walls, not
        // the unsigned angle between their normals. The latter cannot
        // distinguish a 90° corner from the 270° re-entrant corner at
        // world-test-1's (96, 96) edge. Each normal's sign against the other
        // wall's ray identifies whether the common front side is the minor or
        // major sector bounded by those rays.
        auto rayCross = a.ray.x * b.ray.y - a.ray.y * b.ray.x;
        if (std::abs(rayCross) <= ConcavityEpsilon) {
          continue;
        }
        auto aFacesMinor = a.normal.dot(b.ray);
        auto bFacesMinor = b.normal.dot(a.ray);
        auto rayDot = std::clamp(a.ray.dot(b.ray), -1.0f, 1.0f);
        auto minorAngle =
            std::acos(rayDot) * (180.0f / std::acos(-1.0f));
        float wallAngle;
        if (aFacesMinor > ConcavityEpsilon &&
            bFacesMinor > ConcavityEpsilon) {
          wallAngle = minorAngle;
        } else if (
            aFacesMinor < -ConcavityEpsilon &&
            bFacesMinor < -ConcavityEpsilon) {
          wallAngle = 360.0f - minorAngle;
        } else {
          continue;
        }
        if (wallAngle < 225.0f - ConcavityEpsilon ||
            wallAngle > 315.0f + ConcavityEpsilon) {
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
        auto generated = GenerateChips(
            maxZ - minZ, parameters,
            StableVerticalArrisSeed(
                arrangement.vertices[vertexIndex], minZ, maxZ));
        for (auto const& chip : generated) {
          auto depth = std::min({chip.depth, a.length, b.length});
          if (depth <= MinimumChipSize || chip.reach <= MinimumChipSize) {
            continue;
          }
          auto centreZ = minZ + chip.centre;
          auto bottomZ = centreZ - chip.reach * 0.5f;
          auto topZ = centreZ + chip.reach * 0.5f;
          WallNotch proposed{bottomZ, topZ, depth};
          if (!VerticalNotchFits(
                  wallNotches[a.wallIndex], a.atStart, proposed, a.length,
                  wallA.minZ, wallA.maxZ) ||
              !VerticalNotchFits(
                  wallNotches[b.wallIndex], b.atStart, proposed, b.length,
                  wallB.minZ, wallB.maxZ)) {
            continue;
          }
          auto pointA = corner + a.ray * depth;
          auto pointB = corner + b.ray * depth;

          auto addWallNotch = [&](IncidentWall const& incidentWall) {
            auto& notches = wallNotches[incidentWall.wallIndex];
            auto& side = incidentWall.atStart ? notches.left : notches.right;
            side.push_back(proposed);
          };
          addWallNotch(a);
          addWallNotch(b);

          auto sourceWall = std::min(a.wallIndex, b.wallIndex);
          DetailSurfaceKey source{DetailSurfaceKind::Wall, sourceWall};
          Vertex3 onBottom{corner.x, corner.y, bottomZ};
          Vertex3 onTop{corner.x, corner.y, topZ};
          Vertex3 onA{pointA.x, pointA.y, centreZ};
          Vertex3 onB{pointB.x, pointB.y, centreZ};
          // Canonical wall normals point out of the wall material (a Border's
          // point toward its polygon), so the material-side angle bisector is
          // the opposite of their sum. This keeps the gouge centred on the
          // shared edge while moving its deepest point into, never out of, the
          // corner material.
          auto intoMaterial = -(a.normal + b.normal).normalisedCopy();
          auto deepest = corner + intoMaterial * depth;
          Vertex3 atDepth{deepest.x, deepest.y, centreZ};
          // The cavity opens from the deepest point back toward the original
          // shared edge. Select that side of each geometric face normal.
          Vertex3 facetReference{
              -intoMaterial.x, -intoMaterial.y, 0.0f};
          auto const& sourceWallData = walls[sourceWall];
          auto sourceHeight = sourceWallData.maxZ - sourceWallData.minZ;
          auto bottomV = (bottomZ - sourceWallData.minZ) / sourceHeight;
          auto centreV = (centreZ - sourceWallData.minZ) / sourceHeight;
          auto topV = (topZ - sourceWallData.minZ) / sourceHeight;
          std::array<float, 2> uvBottom{0.0f, bottomV};
          std::array<float, 2> uvA{depth / a.length, centreV};
          std::array<float, 2> uvB{depth / b.length, centreV};
          std::array<float, 2> uvTop{0.0f, topV};
          std::array<float, 2> uvDepth{
              (uvA[0] + uvB[0]) * 0.5f, centreV};
          AddTriangle(
              detail, source, onBottom, onA, atDepth, facetReference, uvBottom,
              uvA, uvDepth, false, DetailTriangleKind::VerticalChipFacet);
          AddTriangle(
              detail, source, onA, onTop, atDepth, facetReference, uvA, uvTop,
              uvDepth, false, DetailTriangleKind::VerticalChipFacet);
          AddTriangle(
              detail, source, onTop, onB, atDepth, facetReference, uvTop, uvB,
              uvDepth, false, DetailTriangleKind::VerticalChipFacet);
          AddTriangle(
              detail, source, onB, onBottom, atDepth, facetReference, uvB,
              uvBottom, uvDepth, false,
              DetailTriangleKind::VerticalChipFacet);
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
    AddRebuiltFaceHorizontal(detail, arrangement, key, footprints);
  }

  detail.sort();
  return detail;
}
}  // namespace bw::core::arr
