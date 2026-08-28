#pragma once

#include <core/Arrangement.h>

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
