#pragma once

#include <core/Arrangement.h>
#include <core/Chips.h>

struct WallPhysicalUv {
  float u0{};
  float u1{1.0f};
  float minV{};
  float maxV{1.0f};
};

// Physical wall UVs are independent of procedural Sub-material coordinates.
// U is the absolute projection onto the canonical wall tangent; V is absolute
// world elevation. Only mapped walls consume them.
[[nodiscard]] inline WallPhysicalUv CalculateWallPhysicalUv(
    bw::core::arr::ArrangementWallOrientation const& orientation,
    bw::core::arr::ArrangementWall const& wall) {
  auto image = wall.normalMapOverride.imageData();
  if (!image) return {};
  auto tangent = (orientation.v1 - orientation.v0).normalisedCopy();
  return {orientation.v0.dot(tangent) / image->unitsPerRepeat,
          orientation.v1.dot(tangent) / image->unitsPerRepeat,
          wall.minZ / image->unitsPerRepeat,
          wall.maxZ / image->unitsPerRepeat};
}

// Chip generation triangulates a bitten wall in its own normalized UV space.
// Its coplanar SurfaceRemainder is still the authored wall surface, however,
// so restore the same absolute physical anchoring used by an uncut wall.
// Newly exposed Chip facets deliberately keep their supplied UVs and render
// without the wall normal-map variant.
inline void ApplyWallPhysicalUvToRemainder(
    bw::core::arr::ArrangementWallOrientation const& orientation,
    bw::core::arr::ArrangementWall const& wall,
    bw::core::arr::DetailTriangle& triangle) {
  auto image = wall.normalMapOverride.imageData();
  if (!image ||
      triangle.kind != bw::core::arr::DetailTriangleKind::SurfaceRemainder) {
    return;
  }
  auto tangent = (orientation.v1 - orientation.v0).normalisedCopy();
  for (auto& vertex : triangle.v) {
    wp::Vector2 position{vertex.position[0], vertex.position[1]};
    vertex.uv = {position.dot(tangent) / image->unitsPerRepeat,
                 vertex.position[2] / image->unitsPerRepeat};
  }
}
