#include "core/ArrangementWorldData.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <willpower/common/BoundingCircle.h>
#include <willpower/common/MathsUtils.h>

#include "common/GameDefines.h"

namespace bw::core {
namespace {
wp::Vector2 ToWorld(arr::FixedPointVertex const& vertex) {
  return {
      arr::ToWorldCoordinate(vertex.x),
      arr::ToWorldCoordinate(vertex.y)};
}

std::unique_ptr<ImmutableAccelerationGrid> CreateGrid(
    wp::BoundingBox const& extents,
    float targetCellSize,
    std::span<ImmutableAccelerationGrid::ItemBounds const> itemBounds) {
  auto size = extents.getSize();
  auto dimensionsX = std::max(1, int(size.x / targetCellSize));
  auto dimensionsY = std::max(1, int(size.y / targetCellSize));
  return std::make_unique<ImmutableAccelerationGrid>(
      extents.getMinExtent(), size, dimensionsX, dimensionsY, itemBounds);
}

std::optional<float> TriangleHeightAt(
    arr::DetailTriangle const& triangle,
    wp::Vector2 const& position) {
  auto const& a = triangle.v[0].position;
  auto const& b = triangle.v[1].position;
  auto const& c = triangle.v[2].position;
  auto denominator =
      (b[1] - c[1]) * (a[0] - c[0]) +
      (c[0] - b[0]) * (a[1] - c[1]);
  if (std::abs(denominator) <= 1.0e-8f) return std::nullopt;
  auto u = ((b[1] - c[1]) * (position.x - c[0]) +
            (c[0] - b[0]) * (position.y - c[1])) /
           denominator;
  auto v = ((c[1] - a[1]) * (position.x - c[0]) +
            (a[0] - c[0]) * (position.y - c[1])) /
           denominator;
  auto w = 1.0f - u - v;
  constexpr float Epsilon = 0.0001f;
  if (u < -Epsilon || v < -Epsilon || w < -Epsilon) {
    return std::nullopt;
  }
  return u * a[2] + v * b[2] + w * c[2];
}

wp::BoundingBox VertexGridExtents(
    wp::BoundingBox const& extents,
    float targetCellSize,
    std::vector<arr::FixedPointVertex> const& vertices) {
  auto minExtent = extents.getMinExtent();
  auto maxExtent = extents.getMaxExtent();
  for (auto const& vertex : vertices) {
    auto position = ToWorld(vertex);
    minExtent.x = std::min(minExtent.x, position.x);
    minExtent.y = std::min(minExtent.y, position.y);
    maxExtent.x = std::max(maxExtent.x, position.x);
    maxExtent.y = std::max(maxExtent.y, position.y);
  }
  maxExtent += {targetCellSize, targetCellSize};
  return {minExtent, maxExtent - minExtent};
}
}  // namespace

ArrangementWorldData::ArrangementWorldData(
    arr::ArrangementResultPtr arrangement,
    wp::BoundingBox const& extents,
    float gridCellSize,
    float stepThreshold,
    ArrangementStats* stats,
    WedgeGenerationParameters const& wedgeGenerationParameters)
    : mArrangement(std::move(arrangement)),
      mTriangles(arr::BuildArrangementTriangles(*mArrangement)),
      mWalls(arr::BuildArrangementWalls(*mArrangement)),
      // Built here, on whichever thread constructs the snapshot - the
      // generation worker in game - and immutable from then on, exactly like
      // the two outputs above. Neither of those is altered by its presence.
      mDetail(arr::BuildChipDetail(
          *mArrangement, mWalls, wedgeGenerationParameters)),
      mStepThreshold(stepThreshold),
      mWedgeGenerationParameters(wedgeGenerationParameters) {
  if (stats != nullptr) {
    stats->triangleCount = uint32_t(mTriangles.size());
    stats->wallCount = uint32_t(mWalls.size());
    stats->chipCount = mDetail.getChipCount();
    stats->wedgeCount = mDetail.getWedgeCount();
  }

  std::vector<ImmutableAccelerationGrid::ItemBounds> triangleBounds;
  triangleBounds.reserve(mTriangles.size());
  for (auto const& triangle : mTriangles) {
    auto a = ToWorld(mArrangement->vertices[triangle.v[0]]);
    auto b = ToWorld(mArrangement->vertices[triangle.v[1]]);
    auto c = ToWorld(mArrangement->vertices[triangle.v[2]]);
    triangleBounds.push_back({{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y})},
                              {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y})}});
  }
  mTriangleGrid = CreateGrid(extents, gridCellSize, triangleBounds);

  std::vector<ImmutableAccelerationGrid::ItemBounds> floorWedgeBounds;
  for (uint32_t detailIndex = 0;
       detailIndex < uint32_t(mDetail.getTriangles().size()); ++detailIndex) {
    auto const& triangle = mDetail.getTriangles()[detailIndex];
    if (triangle.kind != arr::DetailTriangleKind::WedgeFacet ||
        triangle.source.kind != arr::DetailSurfaceKind::FloorOfFace) {
      continue;
    }
    auto const& a = triangle.v[0].position;
    auto const& b = triangle.v[1].position;
    auto const& c = triangle.v[2].position;
    floorWedgeBounds.push_back({
        {std::min({a[0], b[0], c[0]}), std::min({a[1], b[1], c[1]})},
        {std::max({a[0], b[0], c[0]}), std::max({a[1], b[1], c[1]})}});
    mFloorWedgeTriangleIndices.push_back(detailIndex);
  }
  mFloorWedgeGrid = CreateGrid(extents, gridCellSize, floorWedgeBounds);

  std::vector<ImmutableAccelerationGrid::ItemBounds> vertexBounds;
  vertexBounds.reserve(mArrangement->vertices.size());
  for (auto const& fixedVertex : mArrangement->vertices) {
    auto vertex = ToWorld(fixedVertex);
    vertexBounds.push_back({vertex, vertex});
  }
  auto vertexExtents = VertexGridExtents(
      extents, gridCellSize, mArrangement->vertices);
  mVertexGrid = CreateGrid(vertexExtents, gridCellSize, vertexBounds);

  std::vector<ImmutableAccelerationGrid::ItemBounds> wallBounds;
  wallBounds.reserve(mWalls.size());
  mCollisionWallIndices.reserve(mWalls.size());
  for (uint32_t wallIndex = 0; wallIndex < uint32_t(mWalls.size());
       ++wallIndex) {
    auto const& wall = mWalls[wallIndex];
    auto const& edge = mArrangement->edges[wall.edge];
    // A floor step above the player's maximum step height may block an
    // ascent regardless of an authored collision override, so retain it as
    // a candidate; traversal queries later remove it when approached from
    // the higher face. An authored override replaces the generated
    // Border/Step default, while Step clearance remains a physical limit.
    auto exceedsStepThreshold =
        wall.kind == arr::ArrangementWallKind::FloorStep &&
        wall.maxZ - wall.minZ > stepThreshold;
    auto authoredCollision = edge.collidesOverride.value_or(
        wall.kind == arr::ArrangementWallKind::Border);
    auto hasInsufficientClearance =
        wall.kind != arr::ArrangementWallKind::Border &&
        wall.clearance < BW_PLAYER_HEIGHT;
    auto blocks = authoredCollision || exceedsStepThreshold ||
                  hasInsufficientClearance;
    if (!blocks) {
      continue;
    }
    auto a = ToWorld(mArrangement->vertices[edge.v[0]]);
    auto b = ToWorld(mArrangement->vertices[edge.v[1]]);
    wallBounds.push_back({{std::min(a.x, b.x), std::min(a.y, b.y)},
                          {std::max(a.x, b.x), std::max(a.y, b.y)}});
    mCollisionWallIndices.push_back(wallIndex);
  }
  mWallGrid = CreateGrid(extents, gridCellSize, wallBounds);
}

arr::ArrangementResult const& ArrangementWorldData::getArrangement() const {
  return *mArrangement;
}

std::vector<arr::ArrangementTriangle> const&
ArrangementWorldData::getTriangles() const {
  return mTriangles;
}

std::vector<arr::ArrangementWall> const& ArrangementWorldData::getWalls() const {
  return mWalls;
}

arr::DetailGeometry const& ArrangementWorldData::getDetail() const {
  return mDetail;
}

WedgeGenerationParameters const&
ArrangementWorldData::getWedgeGenerationParameters() const {
  return mWedgeGenerationParameters;
}

int32_t ArrangementWorldData::pointInTriangle(
    wp::Vector2 const& position) const {
  int cellX, cellY;
  mTriangleGrid->getContainingCell(
      true, position.x, position.y, cellX, cellY);
  if (cellX < 0 || cellY < 0) {
    return -1;
  }
  for (auto triangleIndex : mTriangleGrid->getCellItems(cellX, cellY)) {
    auto const& triangle = mTriangles[triangleIndex];
    auto a = ToWorld(mArrangement->vertices[triangle.v[0]]);
    auto b = ToWorld(mArrangement->vertices[triangle.v[1]]);
    auto c = ToWorld(mArrangement->vertices[triangle.v[2]]);
    if (wp::MathsUtils::pointInTriangle(position, a, b, c)) {
      return int32_t(triangleIndex);
    }
  }
  return -1;
}

uint32_t ArrangementWorldData::getContainingFaceIndex(
    wp::Vector2 const& position) const {
  auto triangleIndex = pointInTriangle(position);
  return triangleIndex < 0 ? ~0u : mTriangles[triangleIndex].face;
}

uint32_t ArrangementWorldData::getContainingPrimitiveIndex(
    wp::Vector2 const& position) const {
  auto faceIndex = getContainingFaceIndex(position);
  return faceIndex == ~0u
             ? ~0u
             : mArrangement->faces[faceIndex].primitiveIndex;
}

int32_t ArrangementWorldData::getNearestVertexIndex(
    wp::Vector2 const& position,
    float radius) const {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(radius)) {
    int32_t result = -1;
    float nearest = radius;
    for (uint32_t i = 0; i < uint32_t(mArrangement->vertices.size()); ++i) {
      auto distance = position.distanceTo(ToWorld(mArrangement->vertices[i]));
      if (distance <= nearest) {
        nearest = distance;
        result = int32_t(i);
      }
    }
    return result;
  }

  wp::BoundingCircle bounds(position, radius);
  ImmutableAccelerationGrid::IndexCollection candidates;
  mVertexGrid->getCandidateItemsInBoundingArea(bounds, candidates);

  int32_t result = -1;
  float nearest = radius;
  for (auto vertexIndex : candidates) {
    auto distance =
        position.distanceTo(ToWorld(mArrangement->vertices[vertexIndex]));
    if (distance <= nearest) {
      nearest = distance;
      result = int32_t(vertexIndex);
    }
  }
  return result;
}

float ArrangementWorldData::getFloorHeight(
    wp::Vector2 const& position) const {
  auto faceIndex = getContainingFaceIndex(position);
  if (faceIndex == ~0u) {
    return -std::numeric_limits<float>::infinity();
  }
  auto height =
      mArrangement->palette[mArrangement->faces[faceIndex].paletteIndex]
          .floorZ;

  int cellX, cellY;
  mFloorWedgeGrid->getContainingCell(
      true, position.x, position.y, cellX, cellY);
  if (cellX < 0 || cellY < 0) return height;
  for (auto floorWedgeIndex :
       mFloorWedgeGrid->getCellItems(cellX, cellY)) {
    auto detailIndex = mFloorWedgeTriangleIndices[floorWedgeIndex];
    auto const& triangle = mDetail.getTriangles()[detailIndex];
    // A floor Wedge can only raise collision above its source face. Taking the
    // maximum also resolves shared fan edges and intentional Wedge overlap.
    if (auto wedgeHeight = TriangleHeightAt(triangle, position)) {
      height = std::max(height, *wedgeHeight);
    }
  }
  return height;
}

float ArrangementWorldData::getCeilingHeight(
    wp::Vector2 const& position) const {
  auto faceIndex = getContainingFaceIndex(position);
  return faceIndex == ~0u
             ? std::numeric_limits<float>::infinity()
             : mArrangement->palette[mArrangement->faces[faceIndex].paletteIndex]
                   .ceilingZ;
}

std::vector<uint32_t> ArrangementWorldData::getWallsNear(
    wp::Vector2 const& position,
    float radius) const {
  wp::BoundingCircle bounds(position, radius);
  ImmutableAccelerationGrid::IndexCollection candidates;
  mWallGrid->getCandidateItemsInBoundingArea(bounds, candidates);
  std::vector<uint32_t> result;
  result.reserve(candidates.size());
  for (auto collisionWallIndex : candidates) {
    result.push_back(mCollisionWallIndices[collisionWallIndex]);
  }
  return result;
}

std::vector<uint32_t> ArrangementWorldData::getWallsNearForTraversal(
    wp::Vector2 const& position,
    float radius,
    wp::Vector2 const& sourcePosition,
    bool descending) const {
  auto sourceFace = getContainingFaceIndex(sourcePosition);
  auto candidates = getWallsNear(position, radius);
  std::vector<uint32_t> result;
  result.reserve(candidates.size());
  for (auto wallIndex : candidates) {
    auto const& wall = mWalls[wallIndex];
    auto const& edge = mArrangement->edges[wall.edge];
    auto authoredCollision = edge.collidesOverride.value_or(
        wall.kind == arr::ArrangementWallKind::Border);
    auto blocksWithoutStepThreshold =
        authoredCollision ||
        (wall.kind != arr::ArrangementWallKind::Border &&
         wall.clearance < BW_PLAYER_HEIGHT);
    if (blocksWithoutStepThreshold ||
        wall.kind != arr::ArrangementWallKind::FloorStep ||
        wall.maxZ - wall.minZ <= mStepThreshold) {
      if (blocksWithoutStepThreshold) result.push_back(wallIndex);
      continue;
    }

    // Once a fall has begun, the actor may already be horizontally over the
    // lower face while still descending from the ledge. Do not reintroduce
    // the step wall behind it and trap its collider there.
    if (descending) continue;

    // A tall FloorStep otherwise blocks only while approaching it from its
    // lower face. If the source cannot be associated with either adjacent
    // face, retain the wall conservatively rather than accidentally opening
    // an ascent.
    if (sourceFace != edge.face[0] && sourceFace != edge.face[1]) {
      result.push_back(wallIndex);
      continue;
    }
    auto sourceFloor =
        mArrangement->palette[mArrangement->faces[sourceFace].paletteIndex]
            .floorZ;
    if (sourceFloor < wall.maxZ) result.push_back(wallIndex);
  }
  return result;
}

int32_t ArrangementWorldData::circleIntersectsWall(
    wp::Vector2 const& position,
    float radius) const {
  for (auto wallIndex : getWallsNear(position, radius)) {
    auto const& edge = mArrangement->edges[mWalls[wallIndex].edge];
    auto a = ToWorld(mArrangement->vertices[edge.v[0]]);
    auto b = ToWorld(mArrangement->vertices[edge.v[1]]);
    if (position.distanceToLine(a, b) <= radius) {
      return int32_t(wallIndex);
    }
  }
  return -1;
}
}  // namespace bw::core
