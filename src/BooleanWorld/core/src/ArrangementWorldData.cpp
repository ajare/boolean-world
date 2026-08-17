#include "core/ArrangementWorldData.h"

#include <algorithm>
#include <limits>

#include <willpower/common/BoundingCircle.h>
#include <willpower/common/MathsUtils.h>

namespace bw::core {
namespace {
wp::Vector2 ToWorld(arr::FixedPointVertex const& vertex) {
  return {
      arr::ToWorldCoordinate(vertex.x),
      arr::ToWorldCoordinate(vertex.y)};
}

std::unique_ptr<wp::AccelerationGrid> CreateGrid(
    wp::BoundingBox const& extents,
    float targetCellSize) {
  auto size = extents.getSize();
  auto dimensionsX = std::max(1, int(size.x / targetCellSize));
  auto dimensionsY = std::max(1, int(size.y / targetCellSize));
  return std::make_unique<wp::AccelerationGrid>(
      extents.getMinExtent(), size, dimensionsX, dimensionsY, 0.0f);
}
}  // namespace

ArrangementWorldData::ArrangementWorldData(
    arr::ArrangementResultPtr arrangement,
    wp::BoundingBox const& extents,
    float gridCellSize,
    float stepThreshold,
    ArrangementStats* stats)
    : mArrangement(std::move(arrangement)),
      mTriangles(arr::BuildArrangementTriangles(*mArrangement)),
      mWalls(arr::BuildArrangementWalls(*mArrangement)),
      mTriangleGrid(CreateGrid(extents, gridCellSize)),
      mWallGrid(CreateGrid(extents, gridCellSize)) {
  if (stats != nullptr) {
    stats->triangleCount = uint32_t(mTriangles.size());
    stats->wallCount = uint32_t(mWalls.size());
  }

  for (uint32_t triangleIndex = 0;
       triangleIndex < uint32_t(mTriangles.size()); ++triangleIndex) {
    auto const& triangle = mTriangles[triangleIndex];
    std::vector<wp::Vector2> vertices;
    for (auto vertexIndex : triangle.v) {
      vertices.push_back(ToWorld(mArrangement->vertices[vertexIndex]));
    }
    mTriangleGrid->addItem(triangleIndex, wp::BoundingBox(vertices));
  }

  for (uint32_t wallIndex = 0; wallIndex < uint32_t(mWalls.size());
       ++wallIndex) {
    auto const& wall = mWalls[wallIndex];
    auto blocks = wall.kind == arr::ArrangementWallKind::Border ||
                  (wall.kind == arr::ArrangementWallKind::FloorStep &&
                   wall.maxZ - wall.minZ > stepThreshold);
    if (!blocks) {
      continue;
    }
    auto const& edge = mArrangement->edges[wall.edge];
    auto a = ToWorld(mArrangement->vertices[edge.v[0]]);
    auto b = ToWorld(mArrangement->vertices[edge.v[1]]);
    mWallGrid->addItem(
        uint32_t(mCollisionWallIndices.size()), wp::BoundingBox({a, b}));
    mCollisionWallIndices.push_back(wallIndex);
  }
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

int32_t ArrangementWorldData::pointInTriangle(
    wp::Vector2 const& position) const {
  int cellX, cellY;
  mTriangleGrid->getContainingCell(
      true, position.x, position.y, cellX, cellY);
  if (cellX < 0 || cellY < 0) {
    return -1;
  }
  for (auto triangleIndex : mTriangleGrid->_getCellItems(cellX, cellY)) {
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

float ArrangementWorldData::getFloorHeight(
    wp::Vector2 const& position) const {
  auto faceIndex = getContainingFaceIndex(position);
  return faceIndex == ~0u
             ? -std::numeric_limits<float>::infinity()
             : mArrangement->palette[mArrangement->faces[faceIndex].paletteIndex]
                   .floorZ;
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
  wp::AccelerationGrid::IndexCollection candidates;
  mWallGrid->getCandidateItemsInBoundingArea(bounds, candidates);
  std::vector<uint32_t> result;
  result.reserve(candidates.size());
  for (auto collisionWallIndex : candidates) {
    result.push_back(mCollisionWallIndices[collisionWallIndex]);
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
