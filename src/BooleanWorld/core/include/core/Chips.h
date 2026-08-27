#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Arrangement.h"
#include "core/Platform.h"

namespace bw::core::arr {
// Which single rendered surface a detail entry refers to.
//
// A renderer emits *two* surfaces from one ArrangementTriangle - a floor at
// the face's floorZ and a ceiling at its ceilingZ - so a key names the side,
// never the triangle: suppressing a triangle outright would punch a matching
// hole in the surface on the other side of the face. Walls emit one quad
// each, so a wall key names the wall.
enum struct DetailSurfaceKind : uint8_t {
  FloorOfFace,
  CeilingOfFace,
  Wall
};

// index is an ArrangementResult::faces index for FloorOfFace/CeilingOfFace,
// and an index into the wall list BuildArrangementWalls produced for Wall.
struct DetailSurfaceKey {
  DetailSurfaceKind kind;
  uint32_t index;

  auto operator<=>(DetailSurfaceKey const& other) const = default;
};

// A replacement vertex carries everything a renderer needs explicitly,
// because a Chip introduces positions that are in no arrangement vertex.
// Positions are in arrangement space with Z up, the same space
// ToWorldCoordinate produces; each renderer applies its own axis mapping.
struct DetailVertex {
  std::array<float, 3> position;
  std::array<float, 3> normal;
  std::array<float, 2> uv;
};

// Vertices are ordered counter-clockwise about `normal` in arrangement space
// (right-handed, Z up).
struct DetailTriangle {
  DetailSurfaceKey source;
  std::array<DetailVertex, 3> v;

  // Wall remainder triangles follow the wall's player-facing side and may be
  // mirrored by the renderer. Chip facets are real outward-facing surfaces
  // and must retain their face normal regardless of which side of the
  // adjoining vertical wall faces the player.
  bool followsWallFacing{false};
};

// The detail channel published alongside mTriangles/mWalls (ADR-0027): the
// surfaces a renderer must not draw, and the triangles that stand in for
// them. A replacement triangle is tagged with the surface it replaces, so a
// wall's replacements inherit that wall's per-frame authored/back-face
// material decision instead of baking one in at generation time.
class BW_API DetailGeometry {
  // Both sorted by key, so the queries below can binary-search them.
  std::vector<DetailSurfaceKey> mSuppressed;
  std::vector<DetailTriangle> mTriangles;
  uint32_t mChipCount{0};

public:
  void addSuppressed(DetailSurfaceKey const& key);
  void addTriangle(DetailTriangle const& triangle);
  void countChip();
  // Restores the sorted-by-key invariant the queries rely on. Called once,
  // by the builder, after every entry has been added.
  void sort();

  [[nodiscard]] bool isSuppressed(
      DetailSurfaceKind kind,
      uint32_t index) const;

  [[nodiscard]] std::span<DetailTriangle const> replacementsFor(
      DetailSurfaceKind kind,
      uint32_t index) const;

  [[nodiscard]] std::vector<DetailSurfaceKey> const& getSuppressed() const;

  [[nodiscard]] std::vector<DetailTriangle> const& getTriangles() const;

  [[nodiscard]] uint32_t getChipCount() const;
};

// Cuts one Chip into the centre of every eligible convex Arris and returns
// the detail channel that replaces the surfaces they bit into.
//
// Eligible here means a visible wall's convex Arris: a FloorStep's top,
// where the wall meets the floor of the higher of its two faces, or a
// CeilingStep's bottom, where the wall meets the ceiling of the lower of its
// two faces. A FloorStep's bottom Arris, a CeilingStep's top, and both of a
// Border wall's, are concave and never chip.
//
// A Chip clamps to fit rather than breaking through the geometry it is cut
// into: depth shrinks so it can never eat through the far side of its own
// step nor break through to the horizontal face's nearest other boundary, and
// reach shrinks so it never runs past either end of the Arris. Whichever
// clamp is most restrictive wins; a Chip clamped below the minimum size is
// dropped rather than emitted as a sliver.
[[nodiscard]] DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls);
}  // namespace bw::core::arr
