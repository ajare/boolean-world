#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <willpower/common/Vector2.h>

#include <core/Arrangement.h>

// One constant Projection normal over an elementary elevation span at an
// Arrangement vertex. Adjacent spans are deliberately not merged: their
// incident-wall sets can differ even when their resulting normals happen to
// be equal.
struct TriplanarWallProjectionSpan {
  float lowerElevation{};
  float upperElevation{};
  wp::Vector2 projectionNormal{};
};

struct TriplanarWallProjectionEndpoint {
  std::optional<uint32_t> arrangementVertex;
  std::vector<TriplanarWallProjectionSpan> spans;
};

// Render-only data parallel to ArrangementWorldData::getWalls(). It does not
// modify the ArrangementWall or any gameplay-facing snapshot data.
struct TriplanarWallProjection {
  bool usesTriplanar{};
  std::array<TriplanarWallProjectionEndpoint, 2> endpoints;
};

using TriplanarWallProjectionData = std::vector<TriplanarWallProjection>;

// Builds deterministic, material-scoped incidence groups at actual
// Arrangement vertices. Geometrically equal positions with different vertex
// identities are intentionally unrelated.
[[nodiscard]] TriplanarWallProjectionData BuildTriplanarWallProjectionData(
    bw::core::arr::ArrangementResult const& arrangement,
    std::vector<bw::core::arr::ArrangementWall> const& walls);

struct TriplanarWallRenderVertex {
  wp::Vector2 position;
  float elevation{};
  wp::Vector2 projectionNormal{};
  float u{};
  float v{};
};

struct TriplanarWallRenderTriangle {
  std::array<TriplanarWallRenderVertex, 3> vertices;
};

// Splits one Triplanar wall only in render data at every elevation where an
// endpoint's incident-wall set changes. The returned triangles retain the
// original flat geometric wall plane; only Projection normals interpolate.
[[nodiscard]] std::vector<TriplanarWallRenderTriangle>
BuildTriplanarWallRenderTriangles(
    bw::core::arr::ArrangementResult const& arrangement,
    bw::core::arr::ArrangementWall const& wall,
    TriplanarWallProjection const& projection);
