#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <willpower/common/BoundingBox.h>
#include <willpower/common/Vector2.h>

#include "core/Arrangement.h"
#include "core/AudioEmitter.h"
#include "core/Chips.h"
#include "core/ImmutableAccelerationGrid.h"
#include "core/LiquidType.h"
#include "core/Platform.h"
#include "core/Stats.h"
#include "core/WedgeGenerationParameters.h"

namespace wp::wayfinder {
class Mesh;
}

namespace bw::core {
class BW_API ArrangementWorldData {
  arr::ArrangementResultPtr mArrangement;
  std::vector<arr::ArrangementTriangle> mTriangles;
  std::vector<arr::ArrangementWall> mWalls;
  // Post-fold detail geometry. Chips and ceiling Wedges remain render-only;
  // floor Wedge facets additionally contribute to floor collision height.
  arr::DetailGeometry mDetail;
  // One settled liquid depth per face - see arr::ComputeLiquidLevels.
  std::vector<float> mLiquidDepths;
  std::vector<uint32_t> mFloorWedgeTriangleIndices;
  std::vector<uint32_t> mCollisionWallIndices;
  std::vector<uint32_t> mRenderedWallIndices;
  WedgeGenerationParameters mWedgeGenerationParameters;
  std::unique_ptr<ImmutableAccelerationGrid> mTriangleGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mFloorWedgeGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mVertexGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mWallGrid;
  // Rendered walls rather than colliding ones: sight and light are blocked by
  // what a wall draws, which is a different set from what it stops an actor
  // walking through.
  std::unique_ptr<ImmutableAccelerationGrid> mRenderedWallGrid;
  std::shared_ptr<wp::wayfinder::Mesh> mWayfinderMesh;
  std::vector<CapturedAudioEmitter> mCapturedAudioEmitters;

public:
  ArrangementWorldData(
      arr::ArrangementResultPtr arrangement,
      wp::BoundingBox const& extents,
      float gridCellSize,
      ArrangementStats* stats = nullptr,
      WedgeGenerationParameters const& wedgeGenerationParameters = {},
      bool createWayfinderMesh = false);

  // Present when navigation generation was requested and the arrangement has
  // at least one solid polygon.
  [[nodiscard]] wp::wayfinder::Mesh* getWayfinderMesh() const;

  [[nodiscard]] arr::ArrangementResult const& getArrangement() const;

  [[nodiscard]] std::vector<arr::ArrangementTriangle> const&
  getTriangles() const;

  [[nodiscard]] std::vector<arr::ArrangementWall> const& getWalls() const;

  // Post-fold detail: Chip replacements and additive Wedge facets. Renderers
  // consume all entries; floor-height collision also consumes floor Wedges.
  [[nodiscard]] arr::DetailGeometry const& getDetail() const;

  [[nodiscard]] std::vector<CapturedAudioEmitter> const&
  getCapturedAudioEmitters() const;

  [[nodiscard]] WedgeGenerationParameters const&
  getWedgeGenerationParameters() const;

  [[nodiscard]] int32_t pointInTriangle(wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingFaceIndex(
      wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingPrimitiveIndex(
      wp::Vector2 const& position) const;

  [[nodiscard]] int32_t getNearestVertexIndex(
      wp::Vector2 const& position,
      float radius) const;

  // Returns the planar face floor raised to the highest containing floor
  // Wedge facet, which is the vertical collision surface used by gameplay.
  [[nodiscard]] float getFloorHeight(wp::Vector2 const& position) const;

  [[nodiscard]] float getCeilingHeight(wp::Vector2 const& position) const;

  // The settled liquid depth at position - see arr::ComputeLiquidLevels. Zero
  // outside the arrangement or wherever no liquid reaches.
  [[nodiscard]] float getLiquidDepth(wp::Vector2 const& position) const;

  // One settled liquid depth per face, parallel to arr::Arrangement's faces
  // and directly indexable by an ArrangementTriangle's face - see
  // arr::ComputeLiquidLevels. Zero means dry.
  [[nodiscard]] std::vector<float> const& getLiquidDepths() const;

  // The world-space height of the settled liquid surface at position - the
  // containing face's own (un-Wedge-raised) floorZ plus its liquid depth,
  // matching the surface WorldRenderer draws. Negative infinity outside the
  // arrangement or wherever no liquid reaches.
  [[nodiscard]] float getLiquidSurfaceHeight(wp::Vector2 const& position) const;

  // The LiquidType of whichever Primitive's properties won the containing
  // face - see PrimitivePropertySet::liquidType. Meaningful only where
  // getLiquidDepth is greater than zero.
  [[nodiscard]] LiquidType getLiquidType(wp::Vector2 const& position) const;

  [[nodiscard]] std::vector<uint32_t> getWallsNear(
      wp::Vector2 const& position,
      float radius) const;

  // Filters nearby collision walls for movement beginning at sourcePosition.
  // A FloorStep above BW_PLAYER_STEP_HEIGHT blocks from its lower face but not
  // from its upper face or while the actor is already descending; authored
  // collision and clearance constraints still apply in both directions.
  [[nodiscard]] std::vector<uint32_t> getWallsNearForTraversal(
      wp::Vector2 const& position,
      float radius,
      wp::Vector2 const& sourcePosition,
      bool descending = false) const;

  [[nodiscard]] int32_t circleIntersectsWall(
      wp::Vector2 const& position,
      float radius) const;

  // How far a horizontal ray at `height` gets from `from` toward `to` before
  // the nearest rendered wall blocks it. Empty when it reaches `to` in the
  // clear. A wall blocks only over its own minZ..maxZ span, so the ray passes
  // above a low FloorStep and below a high CeilingStep exactly as light
  // leaving that height does, and a wall the World does not draw blocks
  // nothing. Collision is a separate question - see getWallsNear.
  [[nodiscard]] std::optional<float> distanceToFirstWallCrossing(
      wp::Vector2 const& from,
      wp::Vector2 const& to,
      float height) const;
};

using ArrangementWorldDataPtr = std::shared_ptr<ArrangementWorldData const>;
}  // namespace bw::core
