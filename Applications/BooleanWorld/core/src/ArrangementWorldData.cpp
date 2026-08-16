#include "core/ArrangementWorldData.h"

#include <algorithm>
#include <limits>

#include <willpower/common/BoundingCircle.h>
#include <willpower/common/MathsUtils.h>

#include "core/ClipperDefines.h"

namespace bw::core {
namespace {
wp::Vector2 ToWorld(expr::Vertex const& vertex) {
  return {
      float(vertex.x / BW_CLIPPER_SCALE),
      float(vertex.y / BW_CLIPPER_SCALE)};
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
    expr::ArrangementResultPtr arrangement,
    wp::BoundingBox const& extents,
    float gridCellSize,
    float stepThreshold)
    : mArrangement(std::move(arrangement)),
      mTriangles(expr::BuildArrangementTriangles(*mArrangement)),
      mWalls(expr::BuildArrangementWalls(*mArrangement)),
      mTriangleGrid(CreateGrid(extents, gridCellSize)),
      mWallGrid(CreateGrid(extents, gridCellSize)) {
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
    auto blocks = wall.kind == expr::ArrangementWallKind::Border ||
                  (wall.kind == expr::ArrangementWallKind::FloorStep &&
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

expr::ArrangementResult const& ArrangementWorldData::getArrangement() const {
  return *mArrangement;
}

std::vector<expr::ArrangementTriangle> const&
ArrangementWorldData::getTriangles() const {
  return mTriangles;
}

std::vector<expr::ArrangementWall> const& ArrangementWorldData::getWalls() const {
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
  auto candidates = mWallGrid->getCandidateItemsInBoundingArea(bounds);
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
