#pragma once

#include <array>

#include <core/Arrangement.h>
#include <core/Chips.h>

struct WallPhysicalUv {
  float u0{};
  float u1{1.0f};
  float minV{};
  float maxV{1.0f};
  std::array<float, 2> bottomV{};
  std::array<float, 2> topV{1.0f, 1.0f};
};

// Physical wall UVs are independent of procedural Sub-material coordinates.
// Each ArrangementWall starts at (0, 0), preventing world-position phase from
// shifting an image partway across the surface. `repeat` is the horizontal
// image count across this wall. V is measured in the same world-space scale;
// the shader then applies the decoded image aspect ratio. A normal map carries
// its own repeat; a mask without a normal map owns a single-tile repeat.
[[nodiscard]] inline float WallImageRepeat(
    bw::core::arr::ArrangementWall const& wall) {
  if (auto image = wall.normalMapOverride.imageData()) {
    return image->repeat;
  }
  return wall.wallMaskOverride.imageData() ? 1.0f : 0.0f;
}

[[nodiscard]] inline WallPhysicalUv CalculateWallPhysicalUv(
    bw::core::arr::ArrangementWallOrientation const& orientation,
    bw::core::arr::ArrangementWall const& wall) {
  auto repeat = WallImageRepeat(wall);
  if (repeat <= 0.0f) return {};
  auto length = orientation.v0.distanceTo(orientation.v1);
  if (length <= 0.0f) return {};
  auto verticalScale = repeat / length;
  WallPhysicalUv result{
      0.0f, repeat, 0.0f, (wall.maxZ - wall.minZ) * verticalScale};
  for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
    result.bottomV[endpoint] =
        (orientation.bottomZ[endpoint] - wall.minZ) * verticalScale;
    result.topV[endpoint] =
        (orientation.topZ[endpoint] - wall.minZ) * verticalScale;
  }
  return result;
}

// Chip generation triangulates a bitten wall in its own normalized UV space.
// Its coplanar SurfaceRemainder is still the authored wall surface, however,
// so restore the same absolute physical anchoring used by an uncut wall.
// Newly exposed Chip facets deliberately keep their supplied UVs and render
// without the wall image variant.
inline void ApplyWallPhysicalUvToRemainder(
    bw::core::arr::ArrangementWallOrientation const& orientation,
    bw::core::arr::ArrangementWall const& wall,
    bw::core::arr::DetailTriangle& triangle) {
  if (triangle.kind != bw::core::arr::DetailTriangleKind::SurfaceRemainder) {
    return;
  }
  auto repeat = WallImageRepeat(wall);
  if (repeat <= 0.0f) return;
  auto tangent = orientation.v1 - orientation.v0;
  auto length = static_cast<float>(tangent.normalise());
  if (length <= 0.0f) return;
  for (auto& vertex : triangle.v) {
    wp::Vector2 position{vertex.position[0], vertex.position[1]};
    vertex.uv = {
        (position - orientation.v0).dot(tangent) * repeat / length,
        (vertex.position[2] - wall.minZ) * repeat / length};
  }
}
