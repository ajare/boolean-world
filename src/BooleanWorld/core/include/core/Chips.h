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

// How far a Chip bites into the horizontal face and, at 45 degrees, equally
// far down the wall (depth), and its total length along the Arris (reach).
// Both in world units.
struct ChipSizes {
  float depth{0.0f};
  float reach{0.0f};
};

// The one size every Chip is cut at for now. ADR-0027 authors depth and
// reach per Sub-material; until that is wired through generation (#281) this
// compiled-in pair stands in for it. Kept inside SubMaterial's authoring
// limits (ChipDepthLimits/ChipReachLimits) so nothing has to change when the
// authored values arrive.
[[nodiscard]] ChipSizes DefaultChipSizes();

// Cuts one Chip into the centre of every eligible convex Arris and returns
// the detail channel that replaces the surfaces they bit into.
//
// Eligible here means a visible FloorStep wall's top Arris, where the wall
// meets the floor of the higher of its two faces. A FloorStep's bottom Arris
// and both of a Border wall's are concave, and a CeilingStep's bottom Arris
// is convex but not yet handled (#279).
//
// Only the wall-height clamp is applied: a Chip's depth shrinks so it can
// never eat through the bottom of its own step. Clamping against the
// horizontal face's other boundaries is #280.
[[nodiscard]] DetailGeometry BuildChipDetail(
    ArrangementResult const& arrangement,
    std::vector<ArrangementWall> const& walls,
    ChipSizes const& sizes = DefaultChipSizes());
}  // namespace bw::core::arr
