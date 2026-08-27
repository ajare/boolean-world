#pragma once

#include <array>
#include <cstdint>
#include <optional>
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
enum struct DetailTriangleKind : uint8_t {
  SurfaceRemainder,
  HorizontalChipFacet,
  VerticalChipFacet,
  CornerChipFacet
};

struct DetailTriangle {
  DetailSurfaceKey source;
  std::array<DetailVertex, 3> v;
  DetailTriangleKind kind{DetailTriangleKind::SurfaceRemainder};

  // Wall remainder triangles follow the wall's player-facing side and may be
  // mirrored by the renderer. Chip facets are real outward-facing surfaces
  // and must retain their face normal regardless of which side of the
  // adjoining vertical wall faces the player.
  bool followsWallFacing{false};

  // Present on Arris Chip facets for diagnostics and tests. Surface
  // remainders and fixed-shape Corner facets leave it empty.
  std::optional<ChipType> chipType;
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

// Generates deterministic Chips along every eligible Arris and returns the
// detail channel that replaces the surfaces they bite into.
//
// Eligible Horizontal Arrises are a visible FloorStep's convex top and a
// visible CeilingStep's convex bottom. Eligible Vertical Arrises are the
// overlapping upright edge of two visible, non-collinear walls whose shared
// canonical-front-side angle is in [225°, 315°] and that use the same
// Sub-material. Eligible Corners are trihedral vertices where two such Step
// walls and their bitten horizontal face meet; a Corner Chip truncates the
// vertex with one independently randomized point on each incident edge.
//
// A Chip clamps to fit rather than breaking through the geometry it is cut
// into. Horizontal Chips respect wall height and the horizontal face's next
// boundary; Vertical Chips respect both walls' lengths. A Corner Chip is
// skipped if its minimum distance does not fit any incident edge; each maximum
// distance is otherwise clamped to its edge. Reach never runs past an Arris
// endpoint, and a Chip below the minimum resulting size is dropped.
[[nodiscard]] DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls);
}  // namespace bw::core::arr
