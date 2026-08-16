#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/Vector2.h>

#include "core/Arrangement.h"
#include "core/Platform.h"

namespace bw::core {
class BW_API ArrangementWorldData {
  arr::ArrangementResultPtr mArrangement;
  std::vector<arr::ArrangementTriangle> mTriangles;
  std::vector<arr::ArrangementWall> mWalls;
  std::vector<uint32_t> mCollisionWallIndices;
  std::unique_ptr<wp::AccelerationGrid> mTriangleGrid;
  std::unique_ptr<wp::AccelerationGrid> mWallGrid;

public:
  ArrangementWorldData(
      arr::ArrangementResultPtr arrangement,
      wp::BoundingBox const& extents,
      float gridCellSize,
      float stepThreshold);

  [[nodiscard]] arr::ArrangementResult const& getArrangement() const;

  [[nodiscard]] std::vector<arr::ArrangementTriangle> const&
  getTriangles() const;

  [[nodiscard]] std::vector<arr::ArrangementWall> const& getWalls() const;

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

  [[nodiscard]] int32_t circleIntersectsWall(
      wp::Vector2 const& position,
      float radius) const;
};

using ArrangementWorldDataPtr = std::shared_ptr<ArrangementWorldData const>;
}  // namespace bw::core
