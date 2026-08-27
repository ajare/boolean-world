#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <willpower/common/BoundingBox.h>
#include <willpower/common/Vector2.h>

#include "core/Arrangement.h"
#include "core/Chips.h"
#include "core/ImmutableAccelerationGrid.h"
#include "core/Platform.h"
#include "core/Stats.h"

namespace bw::core {
class BW_API ArrangementWorldData {
  arr::ArrangementResultPtr mArrangement;
  std::vector<arr::ArrangementTriangle> mTriangles;
  std::vector<arr::ArrangementWall> mWalls;
  // Render-only detail geometry (ADR-0027). Deliberately not read by any
  // query below: collision, floor height, face containment and surface
  // picking all continue to see the unchipped world.
  arr::DetailGeometry mDetail;
  std::vector<uint32_t> mCollisionWallIndices;
  float mStepThreshold;
  std::unique_ptr<ImmutableAccelerationGrid> mTriangleGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mVertexGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mWallGrid;

public:
  ArrangementWorldData(
      arr::ArrangementResultPtr arrangement,
      wp::BoundingBox const& extents,
      float gridCellSize,
      float stepThreshold,
      ArrangementStats* stats = nullptr);

  [[nodiscard]] arr::ArrangementResult const& getArrangement() const;

  [[nodiscard]] std::vector<arr::ArrangementTriangle> const&
  getTriangles() const;

  [[nodiscard]] std::vector<arr::ArrangementWall> const& getWalls() const;

  // The Chip detail channel: which surfaces a renderer must skip, and the
  // triangles standing in for them. Renderers only.
  [[nodiscard]] arr::DetailGeometry const& getDetail() const;

  [[nodiscard]] int32_t pointInTriangle(wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingFaceIndex(
      wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingPrimitiveIndex(
      wp::Vector2 const& position) const;

  [[nodiscard]] int32_t getNearestVertexIndex(
      wp::Vector2 const& position,
      float radius) const;

  [[nodiscard]] float getFloorHeight(wp::Vector2 const& position) const;

  [[nodiscard]] float getCeilingHeight(wp::Vector2 const& position) const;

  [[nodiscard]] std::vector<uint32_t> getWallsNear(
      wp::Vector2 const& position,
      float radius) const;

  // Filters nearby collision walls for movement beginning at sourcePosition.
  // An over-threshold FloorStep blocks from its lower face but not from its
  // upper face or while the actor is already descending; authored collision
  // and clearance constraints still apply in both directions.
  [[nodiscard]] std::vector<uint32_t> getWallsNearForTraversal(
      wp::Vector2 const& position,
      float radius,
      wp::Vector2 const& sourcePosition,
      bool descending = false) const;

  [[nodiscard]] int32_t circleIntersectsWall(
      wp::Vector2 const& position,
      float radius) const;
};

using ArrangementWorldDataPtr = std::shared_ptr<ArrangementWorldData const>;
}  // namespace bw::core
