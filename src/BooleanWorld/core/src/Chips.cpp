#include "core/Chips.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <mapbox/earcut.hpp>

namespace bw::core::arr {
namespace {
// Below this, in world units, a Chip has nothing left to cut and is dropped
// rather than emitted as a sliver.
constexpr float MinimumChipSize = 0.01f;

// Matches BuildArrangementTriangles' floor/ceiling UV scale, so a rebuilt
// face's texture keeps running through it unbroken.
constexpr float HorizontalUvScale = 64.0f;

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

// Emits one replacement triangle, taking its normal from its own geometry and
// ordering its vertices counter-clockwise about that normal. `reference` only
// picks which of the two opposed normals faces out of the solid, so no
// emitter here - the chamfer facet, the wall remainder, or the re-earcut face
// - has to carry winding bookkeeping of its own.
void AddTriangle(
    DetailGeometry& detail,
    DetailSurfaceKey const& source,
    Vertex3 const& p0,
    Vertex3 const& p1,
    Vertex3 const& p2,
    Vertex3 const& reference,
    std::array<float, 2> const& uv0,
    std::array<float, 2> const& uv1,
    std::array<float, 2> const& uv2) {
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
  detail.addTriangle({source, {a, b, c}});
}

// One Chip's footprint on the horizontal face it bit into: the two points
// where it meets the Arris, and the apex it reaches to inside the face.
struct Footprint {
  uint32_t edgeIndex;
  wp::Vector2 a;
  wp::Vector2 b;
  wp::Vector2 apex;
};

// The wall's remainder once the Chip's triangular notch is taken out of its
// top edge, expressed in the wall's own (distance along, height) frame and
// cut into the fewest pieces that leaves no seam.
void AddWallRemainder(
    DetailGeometry& detail,
    DetailSurfaceKey const& source,
    wp::Vector2 const& v0,
    wp::Vector2 const& direction,
    Vertex3 const& reference,
    float length,
    float minZ,
    float maxZ,
    float notchStart,
    float notchEnd,
    float notchDepth) {
  auto height = maxZ - minZ;
  auto notchMiddle = (notchStart + notchEnd) * 0.5f;
  auto notchZ = maxZ - notchDepth;

  auto at = [&](float s, float z) {
    auto position = v0 + direction * s;
    return Vertex3{position.x, position.y, z};
  };
  auto uv = [&](float s, float z) {
    return std::array<float, 2>{s / length, (z - minZ) / height};
  };
  auto quad = [&](float s0, float s1, float z0, float z1) {
    if (s1 - s0 <= MinimumChipSize || z1 - z0 <= MinimumChipSize) {
      return;
    }
    AddTriangle(
        detail, source, at(s0, z0), at(s1, z0), at(s1, z1), reference,
        uv(s0, z0), uv(s1, z0), uv(s1, z1));
    AddTriangle(
        detail, source, at(s0, z0), at(s1, z1), at(s0, z1), reference,
        uv(s0, z0), uv(s1, z1), uv(s0, z1));
  };

  // Everything below the notch's deepest point, then the band the notch sits
  // in, split either side of it.
  quad(0.0f, length, minZ, notchZ);
  quad(0.0f, notchStart, notchZ, maxZ);
  quad(notchEnd, length, notchZ, maxZ);

  // The two corners the notch's sloping sides leave behind inside that band.
  AddTriangle(
      detail, source, at(notchStart, notchZ), at(notchMiddle, notchZ),
      at(notchStart, maxZ), reference, uv(notchStart, notchZ),
      uv(notchMiddle, notchZ), uv(notchStart, maxZ));
  AddTriangle(
      detail, source, at(notchMiddle, notchZ), at(notchEnd, notchZ),
      at(notchEnd, maxZ), reference, uv(notchMiddle, notchZ),
      uv(notchEnd, notchZ), uv(notchEnd, maxZ));
}

// Rebuilds one horizontal face's floor with every Chip footprint on it
// subtracted from its boundary polygon, rather than clipping the individual
// triangles the unchipped face earcut to.
void AddRebuiltFaceFloor(
    DetailGeometry& detail,
    ArrangementResult const& arrangement,
    uint32_t faceIndex,
    std::vector<Footprint> const& footprints) {
  using EarcutPoint = std::array<double, 2>;

  auto const& face = arrangement.faces[faceIndex];
  auto floorZ = arrangement.palette[face.paletteIndex].floorZ;
  DetailSurfaceKey source{DetailSurfaceKind::FloorOfFace, faceIndex};

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
      for (auto const& footprint : footprints) {
        if (footprint.edgeIndex != boundary[i]) {
          continue;
        }
        // The boundary traversal may run either way along the Arris.
        auto aFirst = current.distanceTo(footprint.a) <=
            current.distanceTo(footprint.b);
        auto const& first = aFirst ? footprint.a : footprint.b;
        auto const& second = aFirst ? footprint.b : footprint.a;
        pushDistinct(first, current, next);
        push(footprint.apex);
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

  Vertex3 up{0.0f, 0.0f, 1.0f};
  auto indices = mapbox::earcut<uint32_t>(polygons);
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    Vertex3 p[3];
    std::array<float, 2> uv[3];
    for (int corner = 0; corner < 3; ++corner) {
      auto const& position = positions[indices[i + corner]];
      p[corner] = {position.x, position.y, floorZ};
      uv[corner] = {
          position.x / HorizontalUvScale, position.y / HorizontalUvScale};
    }
    AddTriangle(detail, source, p[0], p[1], p[2], up, uv[0], uv[1], uv[2]);
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

ChipSizes DefaultChipSizes() {
  return {3.0f, 24.0f};
}

DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls,
    ChipSizes const& sizes) {
  DetailGeometry detail;
  if (sizes.depth <= MinimumChipSize || sizes.reach <= MinimumChipSize) {
    return detail;
  }

  std::map<uint32_t, std::vector<Footprint>> footprintsByFace;

  for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
       ++wallIndex) {
    auto const& wall = walls[wallIndex];
    // Only a FloorStep's top Arris is both convex and in scope here. A wall
    // whose visibility override is off is skipped outright rather than
    // having its floor bitten to expose a facet nothing would draw.
    if (wall.kind != ArrangementWallKind::FloorStep || !wall.visible) {
      continue;
    }

    auto const& edge = arrangement.edges[wall.edge];
    auto const& properties0 =
        arrangement.palette[arrangement.faces[edge.face[0]].paletteIndex];
    auto const& properties1 =
        arrangement.palette[arrangement.faces[edge.face[1]].paletteIndex];
    // The Arris runs along the top of the wall, which is the floor of the
    // higher of its two faces.
    auto upperFace =
        properties0.floorZ > properties1.floorZ ? edge.face[0] : edge.face[1];
    if (!arrangement.faces[upperFace].solid) {
      continue;
    }

    auto orientation = OrientArrangementWall(arrangement, wall);
    auto along = orientation.v1 - orientation.v0;
    auto length = along.length();
    if (length <= MinimumChipSize) {
      continue;
    }
    auto direction = along / length;
    // A FloorStep's normal points at its lower face, so the upper face - the
    // one the Chip bites into - lies the other way.
    auto inward = -orientation.normal;

    // The only clamp this ticket applies: a Chip shrinks to fit its wall's
    // height so it can never eat through the bottom of its own step. It is
    // also held inside its own Arris, which is a condition of the geometry
    // existing at all rather than a clamp.
    auto depth = std::min(sizes.depth, wall.maxZ - wall.minZ);
    auto reach = std::min(sizes.reach, length);
    if (depth <= MinimumChipSize || reach <= MinimumChipSize) {
      continue;
    }

    // One Chip per Arris, at its centre, positioned from the Arris's own
    // endpoints - never from the wall or edge index, which renumber on every
    // unrelated edit (ADR-0027).
    auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
    auto half = direction * (reach * 0.5f);
    auto a = midpoint - half;
    auto b = midpoint + half;
    auto apex = midpoint + inward * depth;

    footprintsByFace[upperFace].push_back({wall.edge, a, b, apex});

    DetailSurfaceKey source{DetailSurfaceKind::Wall, wallIndex};
    detail.addSuppressed(source);

    auto notchStart = (length - reach) * 0.5f;
    auto notchEnd = notchStart + reach;
    Vertex3 wallReference{orientation.normal.x, orientation.normal.y, 0.0f};
    AddWallRemainder(
        detail, source, orientation.v0, direction, wallReference, length,
        wall.minZ, wall.maxZ, notchStart, notchEnd, depth);

    // The chamfer itself. The wedge a Chip removes is a tetrahedron: two of
    // its corners sit on the Arris, one on the floor and one down the wall,
    // both a `depth` away, which is what makes the bevel 45 degrees. Its cut
    // surface is therefore two flat triangles meeting along the deepest
    // cross-section, each tapering to a point on the Arris - so the Chip
    // closes on itself and needs no end caps. Both carry the wall's own key,
    // so they follow it into whichever material it draws with this frame,
    // authored or reserved back face, and add no mesh bucket anywhere.
    Vertex3 onArrisA{a.x, a.y, wall.maxZ};
    Vertex3 onArrisB{b.x, b.y, wall.maxZ};
    Vertex3 onFloor{apex.x, apex.y, wall.maxZ};
    Vertex3 onWall{midpoint.x, midpoint.y, wall.maxZ - depth};
    Vertex3 facetReference{orientation.normal.x, orientation.normal.y, 1.0f};
    auto height = wall.maxZ - wall.minZ;
    std::array<float, 2> uvA{notchStart / length, 1.0f};
    std::array<float, 2> uvB{notchEnd / length, 1.0f};
    std::array<float, 2> uvFloor{0.5f, 1.0f};
    std::array<float, 2> uvWall{0.5f, (height - depth) / height};
    AddTriangle(
        detail, source, onArrisA, onFloor, onWall, facetReference, uvA,
        uvFloor, uvWall);
    AddTriangle(
        detail, source, onFloor, onArrisB, onWall, facetReference, uvFloor,
        uvB, uvWall);

    detail.countChip();
  }

  for (auto const& [faceIndex, footprints] : footprintsByFace) {
    AddRebuiltFaceFloor(detail, arrangement, faceIndex, footprints);
  }

  detail.sort();
  return detail;
}
}  // namespace bw::core::arr
