#include "core/ArrangementWorldData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>

#include <willpower/common/BoundingCircle.h>
#include <willpower/common/MathsUtils.h>
#include <willpower/wayfinder/Mesh.h>

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

wp::Vector2 EdgeInteractionPosition(
    wp::Vector2 const& source,
    wp::Vector2 const& destination,
    wp::Vector2 const& edgeStart,
    wp::Vector2 const& edgeEnd) {
  auto movement = destination - source;
  auto edge = edgeEnd - edgeStart;
  auto determinant = movement.x * edge.y - movement.y * edge.x;
  if (std::abs(determinant) > 1.0e-8f) {
    auto offset = edgeStart - source;
    auto alongMovement =
        (offset.x * edge.y - offset.y * edge.x) / determinant;
    auto alongEdge =
        (offset.x * movement.y - offset.y * movement.x) / determinant;
    if (alongMovement >= 0.0f && alongMovement <= 1.0f &&
        alongEdge >= 0.0f && alongEdge <= 1.0f) {
      return source + movement * alongMovement;
    }
  }
  return destination.closestPointOnLine(edgeStart, edgeEnd);
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

std::shared_ptr<wp::wayfinder::Mesh> CreateWayfinderMesh(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementTriangle> const& triangles) {
  if (triangles.empty()) {
    return {};
  }

  std::vector<wp::Vector2> vertices;
  vertices.reserve(arrangement.vertices.size());
  for (auto const& vertex : arrangement.vertices) {
    vertices.push_back(ToWorld(vertex));
  }

  std::vector<wp::wayfinder::Triangle> wayfinderTriangles;
  wayfinderTriangles.reserve(triangles.size());
  for (auto const& triangle : triangles) {
    wayfinderTriangles.push_back(
        {triangle.v[0], triangle.v[1], triangle.v[2]});
  }

  return std::make_shared<wp::wayfinder::Mesh>(
      vertices, wayfinderTriangles);
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
    ArrangementStats* stats,
    WedgeGenerationParameters const& wedgeGenerationParameters,
    bool createWayfinderMesh)
    : mArrangement(std::move(arrangement)),
      mTriangles(arr::BuildArrangementTriangles(*mArrangement)),
      mWalls(arr::BuildArrangementWalls(*mArrangement)),
      // Built here, on whichever thread constructs the snapshot - the
      // generation worker in game - and immutable from then on, exactly like
      // the two outputs above. Neither of those is altered by its presence.
      mDetail(arr::BuildChipDetail(
          *mArrangement, mWalls, wedgeGenerationParameters)),
      mLiquidDepths(arr::ComputeLiquidLevels(*mArrangement)),
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
    floorWedgeBounds.push_back({{std::min({a[0], b[0], c[0]}), std::min({a[1], b[1], c[1]})},
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
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    // A floor step above the player's maximum step height may block an
    // ascent regardless of an authored collision override, so retain it as
    // a candidate; traversal queries later remove it when approached from
    // the higher face. An authored override replaces the generated
    // Border/Step default, while Step clearance remains a physical limit.
    // These are conservative broad-phase predicates only. Exact traversal
    // evaluates both incident surfaces at the attempted crossing below.
    auto exceedsStepHeight =
        wall.kind == arr::ArrangementWallKind::FloorStep &&
        std::max(
            wall.topZ[0] - wall.bottomZ[0],
            wall.topZ[1] - wall.bottomZ[1]) > BW_PLAYER_STEP_HEIGHT;
    auto authoredCollision = edge.collidesOverride.value_or(
        wall.kind == arr::ArrangementWallKind::Border);
    auto hasInsufficientClearance =
        wall.kind != arr::ArrangementWallKind::Border &&
        wall.clearance < BW_PLAYER_HEIGHT;
    auto blocks = authoredCollision || exceedsStepHeight ||
                  hasInsufficientClearance;
    if (!blocks) {
      continue;
    }
    auto const& a = orientation.v0;
    auto const& b = orientation.v1;
    wallBounds.push_back({{std::min(a.x, b.x), std::min(a.y, b.y)},
                          {std::max(a.x, b.x), std::max(a.y, b.y)}});
    mCollisionWallIndices.push_back(wallIndex);
  }
  mWallGrid = CreateGrid(extents, gridCellSize, wallBounds);

  std::vector<ImmutableAccelerationGrid::ItemBounds> renderedWallBounds;
  renderedWallBounds.reserve(mWalls.size());
  mRenderedWallIndices.reserve(mWalls.size());
  for (uint32_t wallIndex = 0; wallIndex < uint32_t(mWalls.size());
       ++wallIndex) {
    auto const& wall = mWalls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    auto const& a = orientation.v0;
    auto const& b = orientation.v1;
    renderedWallBounds.push_back({{std::min(a.x, b.x), std::min(a.y, b.y)},
                                  {std::max(a.x, b.x), std::max(a.y, b.y)}});
    mRenderedWallIndices.push_back(wallIndex);
  }
  mRenderedWallGrid = CreateGrid(extents, gridCellSize, renderedWallBounds);

  // Capture is deliberately after detail geometry and its floor-Wedge index:
  // derived emitter height uses the same raised floor as player collision.
  mCapturedAudioEmitters.reserve(mArrangement->audioEmitters.size());
  mFailedAudioEmitters.reserve(mArrangement->audioEmitters.size());
  for (auto const& emitter : mArrangement->audioEmitters) {
    auto fail = [&](AudioEmitterCaptureFailure reason,
                    std::optional<float> derivedHeight = std::nullopt) {
      mFailedAudioEmitters.push_back(
          {emitter.position, derivedHeight, emitter.heightOffset,
           emitter.soundId, emitter.guid, emitter.cullRadius,
           emitter.placementKey, reason});
    };

    auto surface = getSurfaceSample(emitter.position);
    if (!surface) {
      fail(AudioEmitterCaptureFailure::NoSolidGeometry);
      continue;
    }
    auto height = surface->floorElevation + emitter.heightOffset;
    if (!surface->face->solidContributors.contains(
            emitter.parentPrimitiveIndex)) {
      fail(AudioEmitterCaptureFailure::ParentDoesNotContribute, height);
      continue;
    }
    if (!(height < surface->ceilingElevation)) {
      fail(AudioEmitterCaptureFailure::DerivedHeightAboveCeiling, height);
      continue;
    }
    mCapturedAudioEmitters.push_back(
        {emitter.position, height, emitter.soundId, emitter.guid,
         emitter.cullRadius, emitter.placementKey});
  }

  if (createWayfinderMesh) {
    auto start = std::chrono::steady_clock::now();
    mWayfinderMesh = CreateWayfinderMesh(*mArrangement, mTriangles);
    if (stats != nullptr) {
      stats->wayfinderMeshTimeNs = uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start)
              .count());
    }
  }
}

wp::wayfinder::Mesh* ArrangementWorldData::getWayfinderMesh() const {
  return mWayfinderMesh.get();
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

std::vector<CapturedAudioEmitter> const&
ArrangementWorldData::getCapturedAudioEmitters() const {
  return mCapturedAudioEmitters;
}

std::vector<FailedAudioEmitter> const&
ArrangementWorldData::getFailedAudioEmitters() const {
  return mFailedAudioEmitters;
}

std::vector<float> const& ArrangementWorldData::getLiquidDepths() const {
  return mLiquidDepths;
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

std::optional<SurfaceSample> ArrangementWorldData::getSurfaceSample(
    wp::Vector2 const& position) const {
  return getSurfaceSample(getContainingFaceIndex(position), position);
}

std::optional<SurfaceSample> ArrangementWorldData::getSurfaceSample(
    uint32_t faceIndex,
    wp::Vector2 const& position) const {
  if (faceIndex >= mArrangement->faces.size()) return std::nullopt;

  auto const& face = mArrangement->faces[faceIndex];
  if (!face.solid) return std::nullopt;
  auto const& properties = mArrangement->palette[face.paletteIndex];
  auto floorNormal = properties.floorZ.normal();
  auto ceilingUp = properties.ceilingZ.normal();
  SurfaceSample sample{
      properties.floorZ.evaluate(position),
      properties.ceilingZ.evaluate(position),
      floorNormal,
      {-ceilingUp[0], -ceilingUp[1], -ceilingUp[2]},
      faceIndex,
      &face};

  int cellX, cellY;
  mFloorWedgeGrid->getContainingCell(
      true, position.x, position.y, cellX, cellY);
  if (cellX < 0 || cellY < 0) return sample;
  for (auto floorWedgeIndex :
       mFloorWedgeGrid->getCellItems(cellX, cellY)) {
    auto detailIndex = mFloorWedgeTriangleIndices[floorWedgeIndex];
    auto const& triangle = mDetail.getTriangles()[detailIndex];
    if (triangle.source.index != faceIndex) continue;
    // A floor Wedge can only raise collision above its source face. Taking the
    // maximum also resolves shared fan edges and intentional Wedge overlap.
    if (auto wedgeHeight = TriangleHeightAt(triangle, position);
        wedgeHeight && *wedgeHeight > sample.floorElevation) {
      sample.floorElevation = *wedgeHeight;
      sample.floorNormal = triangle.v[0].normal;
    }
  }
  return sample;
}

float ArrangementWorldData::getFloorHeight(
    wp::Vector2 const& position) const {
  auto sample = getSurfaceSample(position);
  return sample ? sample->floorElevation
                : -std::numeric_limits<float>::infinity();
}

float ArrangementWorldData::getCeilingHeight(
    wp::Vector2 const& position) const {
  auto sample = getSurfaceSample(position);
  return sample ? sample->ceilingElevation
                : std::numeric_limits<float>::infinity();
}

float ArrangementWorldData::getLiquidDepth(wp::Vector2 const& position) const {
  auto surface = getSurfaceSample(position);
  return surface ? mLiquidDepths[surface->faceIndex] : 0.0f;
}

float ArrangementWorldData::getLiquidSurfaceHeight(
    wp::Vector2 const& position) const {
  auto surface = getSurfaceSample(position);
  if (!surface) {
    return -std::numeric_limits<float>::infinity();
  }
  auto liquidDepth = mLiquidDepths[surface->faceIndex];
  if (liquidDepth <= 0.0f) {
    return -std::numeric_limits<float>::infinity();
  }
  // Liquid settlement still stores one depth per flat-world face. Evaluate
  // that face's plane at the query position rather than converting its base
  // elevation as though it were a face-wide height. Floor Wedges do not move
  // the rendered free surface, so use the selected Elevation plane rather
  // than the collision-raised floor in SurfaceSample.
  auto const& properties =
      mArrangement->palette[surface->face->paletteIndex];
  return properties.floorZ.evaluate(position) + liquidDepth;
}

LiquidType ArrangementWorldData::getLiquidType(
    wp::Vector2 const& position) const {
  auto faceIndex = getContainingFaceIndex(position);
  if (faceIndex == ~0u) {
    return LiquidType::Water;
  }
  return mArrangement->palette[mArrangement->faces[faceIndex].paletteIndex]
      .liquidType;
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

bool ArrangementWorldData::wallBlocksTraversalWithoutStepAt(
    uint32_t wallIndex,
    wp::Vector2 const& position) const {
  if (wallIndex >= mWalls.size()) return true;
  auto const& wall = mWalls[wallIndex];
  auto const& edge = mArrangement->edges[wall.edge];
  auto authoredCollision = edge.collidesOverride.value_or(
      wall.kind == arr::ArrangementWallKind::Border);
  if (authoredCollision) return true;
  if (wall.kind == arr::ArrangementWallKind::Border) return false;

  auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
  auto interaction =
      position.closestPointOnLine(orientation.v0, orientation.v1);
  auto side0 = getSurfaceSample(edge.face[0], interaction);
  auto side1 = getSurfaceSample(edge.face[1], interaction);
  if (!side0 || !side1) return true;
  auto clearance =
      std::min(side0->ceilingElevation, side1->ceilingElevation) -
      std::max(side0->floorElevation, side1->floorElevation);
  return clearance < BW_PLAYER_HEIGHT;
}

std::vector<uint32_t> ArrangementWorldData::getWallsNearForTraversal(
    wp::Vector2 const& destinationPosition,
    float radius,
    wp::Vector2 const& sourcePosition,
    bool descending) const {
  auto sourceFace = getContainingFaceIndex(sourcePosition);
  auto candidates = getWallsNear(destinationPosition, radius);
  std::vector<uint32_t> result;
  result.reserve(candidates.size());
  for (auto wallIndex : candidates) {
    auto const& wall = mWalls[wallIndex];
    auto const& edge = mArrangement->edges[wall.edge];
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    auto interaction = EdgeInteractionPosition(
        sourcePosition, destinationPosition, orientation.v0, orientation.v1);
    if (wallBlocksTraversalWithoutStepAt(wallIndex, interaction)) {
      result.push_back(wallIndex);
      continue;
    }
    if (wall.kind != arr::ArrangementWallKind::FloorStep) continue;

    // Once a fall has begun, the actor may already be horizontally over the
    // lower face while still descending from the ledge. Do not reintroduce
    // the step wall behind it and trap its collider there.
    if (descending) continue;

    // A FloorStep blocks only when the exact crossing rises farther than the
    // player's capability. Sampling both incident faces explicitly avoids the
    // arbitrary side selected by a point-in-triangle query on their edge.
    if (sourceFace != edge.face[0] && sourceFace != edge.face[1]) {
      result.push_back(wallIndex);
      continue;
    }
    auto targetFace =
        sourceFace == edge.face[0] ? edge.face[1] : edge.face[0];
    auto source = getSurfaceSample(sourceFace, interaction);
    auto target = getSurfaceSample(targetFace, interaction);
    if (!source || !target ||
        target->floorElevation - source->floorElevation >
            BW_PLAYER_STEP_HEIGHT) {
      result.push_back(wallIndex);
    }
  }
  return result;
}

std::optional<float> ArrangementWorldData::distanceToFirstWallCrossing(
    wp::Vector2 const& from,
    wp::Vector2 const& to,
    float height) const {
  auto ray = to - from;
  auto rayLength = ray.length();
  if (rayLength <= 0.0f) {
    return std::nullopt;
  }

  // BoundingBox normalizes a negative size into its extents, so the ray
  // itself is the box whichever way it points.
  wp::BoundingBox bounds(from, ray);
  ImmutableAccelerationGrid::IndexCollection candidates;
  mRenderedWallGrid->getCandidateItemsInBoundingArea(bounds, candidates);

  // Keep the nearest crossing rather than the first found: grid cells hand
  // back candidates in storage order, not along the ray.
  auto nearest = std::numeric_limits<float>::infinity();
  for (auto renderedWallIndex : candidates) {
    auto const& wall = mWalls[mRenderedWallIndices[renderedWallIndex]];
    auto const& edge = mArrangement->edges[wall.edge];
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    auto const& a = orientation.v0;
    auto const& b = orientation.v1;
    auto wallSpan = b - a;
    auto determinant = ray.x * wallSpan.y - wallSpan.x * ray.y;
    if (std::abs(determinant) <=
        wp::MathsUtils::Epsilon * std::max(ray.lengthSq(), wallSpan.lengthSq())) {
      // Parallel. A ray running along a wall is not crossing it, and one
      // running through its line reaches whatever wall closes the far end.
      continue;
    }
    auto offset = a - from;
    auto alongRay = (offset.x * wallSpan.y - wallSpan.x * offset.y) / determinant;
    auto alongWall = (offset.x * ray.y - ray.x * offset.y) / determinant;
    if (alongRay < 0.0f || alongRay > 1.0f || alongWall < 0.0f ||
        alongWall > 1.0f) {
      continue;
    }
    auto crossing = a + wallSpan * alongWall;
    auto bottom = orientation.bottomZ[0] +
                  (orientation.bottomZ[1] - orientation.bottomZ[0]) *
                      alongWall;
    auto top = orientation.topZ[0] +
               (orientation.topZ[1] - orientation.topZ[0]) * alongWall;
    if (wall.kind != arr::ArrangementWallKind::Border) {
      auto side0 = getSurfaceSample(edge.face[0], crossing);
      auto side1 = getSurfaceSample(edge.face[1], crossing);
      if (!side0 || !side1) continue;
      if (wall.kind == arr::ArrangementWallKind::FloorStep) {
        bottom = std::min(side0->floorElevation, side1->floorElevation);
        top = std::max(side0->floorElevation, side1->floorElevation);
      } else {
        bottom = std::min(
            side0->ceilingElevation, side1->ceilingElevation);
        top = std::max(
            side0->ceilingElevation, side1->ceilingElevation);
      }
    }
    // The span is inclusive: a ray level with the lip of a step is grazing
    // the generated surface, so treat it as blocked. Evaluate that span at
    // the crossing rather than using the wall's conservative broad bounds.
    if (height < bottom || height > top) continue;
    nearest = std::min(nearest, alongRay * rayLength);
  }

  return std::isfinite(nearest) ? std::optional<float>{nearest} : std::nullopt;
}

int32_t ArrangementWorldData::circleIntersectsWall(
    wp::Vector2 const& position,
    float radius) const {
  for (auto wallIndex : getWallsNear(position, radius)) {
    auto const& wall = mWalls[wallIndex];
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    if (position.distanceToLine(orientation.v0, orientation.v1) <= radius) {
      return int32_t(wallIndex);
    }
  }
  return -1;
}

int32_t ArrangementWorldData::circleIntersectsWallForTraversal(
    wp::Vector2 const& destinationPosition,
    float radius,
    wp::Vector2 const& sourcePosition,
    bool descending) const {
  for (auto wallIndex : getWallsNearForTraversal(
           destinationPosition, radius, sourcePosition, descending)) {
    auto const& wall = mWalls[wallIndex];
    auto orientation = arr::OrientArrangementWall(*mArrangement, wall);
    if (destinationPosition.distanceToLine(
            orientation.v0, orientation.v1) <= radius) {
      return int32_t(wallIndex);
    }
  }
  return -1;
}
}  // namespace bw::core
