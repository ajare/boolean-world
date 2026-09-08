#pragma once

#include <array>
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

// The generated surfaces at one World-plane position. `face` points into the
// immutable Arrangement snapshot owned by the ArrangementWorldData that
// produced this value and is valid for the snapshot's lifetime.
struct SurfaceSample {
  float floorElevation;
  float ceilingElevation;
  std::array<float, 3> floorNormal;
  std::array<float, 3> ceilingNormal;
  uint32_t faceIndex;
  arr::ArrangementFace const* face;
};

// One maximal part of a World-plane movement that lies in a single
// Arrangement face. Fractions are ordered along start -> end. Endpoint
// samples are evaluated explicitly in that face, so an Arrangement-edge
// position never depends on point-location tie breaking.
struct SurfaceTraversalSegment {
  float beginFraction;
  float endFraction;
  uint32_t faceIndex;
  std::optional<SurfaceSample> beginSurface;
  std::optional<SurfaceSample> endSurface;
};

class BW_API ArrangementWorldData {
  arr::ArrangementResultPtr mArrangement;
  std::vector<arr::ArrangementTriangle> mTriangles;
  std::vector<arr::ArrangementWall> mWalls;
  // Post-fold detail geometry. Chips and ceiling Wedges remain render-only;
  // floor Wedge facets additionally contribute to floor collision height.
  arr::DetailGeometry mDetail;
  // Hydraulic cells, horizontal Pool elevations, and clipped visible Liquid
  // geometry derived from the generated Arrangement triangles.
  arr::LiquidState mLiquidState;
  std::vector<uint32_t> mFloorWedgeTriangleIndices;
  std::vector<uint32_t> mCollisionWallIndices;
  std::vector<uint32_t> mRenderedWallIndices;
  WedgeGenerationParameters mWedgeGenerationParameters;
  std::unique_ptr<ImmutableAccelerationGrid> mTriangleGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mFloorWedgeGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mVertexGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mEdgeGrid;
  std::unique_ptr<ImmutableAccelerationGrid> mWallGrid;
  // Rendered walls rather than colliding ones: sight and light are blocked by
  // what a wall draws, which is a different set from what it stops an actor
  // walking through.
  std::unique_ptr<ImmutableAccelerationGrid> mRenderedWallGrid;
  std::shared_ptr<wp::wayfinder::Mesh> mWayfinderMesh;
  std::vector<CapturedAudioEmitter> mCapturedAudioEmitters;
  std::vector<FailedAudioEmitter> mFailedAudioEmitters;

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

  // Every authored/generated emitter rejected by capture, together with the
  // first failed limb of the capture rule for editor feedback.
  [[nodiscard]] std::vector<FailedAudioEmitter> const&
  getFailedAudioEmitters() const;

  [[nodiscard]] WedgeGenerationParameters const&
  getWedgeGenerationParameters() const;

  [[nodiscard]] int32_t pointInTriangle(wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingFaceIndex(
      wp::Vector2 const& position) const;

  [[nodiscard]] uint32_t getContainingPrimitiveIndex(
      wp::Vector2 const& position) const;

  // The authoritative position-based contract for generated floor and
  // ceiling geometry. Empty outside generated solid faces. Floor Wedges are
  // included because they alter the collision surface.
  [[nodiscard]] std::optional<SurfaceSample> getSurfaceSample(
      wp::Vector2 const& position) const;

  // Samples a face already identified by a caller at a position on or within
  // it. This is the unambiguous form for Arrangement-edge crossings, where a
  // point belongs to both incident faces and an ordinary containment lookup
  // could select either one.
  [[nodiscard]] std::optional<SurfaceSample> getSurfaceSample(
      uint32_t faceIndex,
      wp::Vector2 const& position) const;

  // Splits a movement at every crossed Arrangement edge and returns the
  // traversed affine face segments in movement order. This is the common
  // basis for local step, support, and sloped-clearance decisions.
  [[nodiscard]] std::vector<SurfaceTraversalSegment> getSurfaceTraversal(
      wp::Vector2 const& start,
      wp::Vector2 const& end) const;

  [[nodiscard]] int32_t getNearestVertexIndex(
      wp::Vector2 const& position,
      float radius) const;

  // Returns the planar face floor raised to the highest containing floor
  // Wedge facet, which is the vertical collision surface used by gameplay.
  [[nodiscard]] float getFloorHeight(wp::Vector2 const& position) const;

  [[nodiscard]] float getCeilingHeight(wp::Vector2 const& position) const;

  // One Hydraulic cell per generated Arrangement triangle and its basin's
  // settled Pool elevation. Pool entries are negative infinity where the
  // basin holds no Liquid; a cell wholly above a shoreline can still share
  // its basin's finite elevation while reporting zero local depth.
  [[nodiscard]] std::vector<arr::HydraulicCell> const&
  getHydraulicCells() const;
  [[nodiscard]] std::vector<double> const& getLiquidPoolElevations() const;

  // Horizontal visible interfaces clipped to the portions of Hydraulic cells
  // where the Pool lies above the affine floor and below the affine ceiling.
  [[nodiscard]] std::vector<arr::LiquidSurfaceTriangle> const&
  getLiquidSurfaceTriangles() const;

  // The settled Liquid depth at position, evaluated below the containing
  // cell's horizontal Pool elevation and clamped to its local affine floor and
  // ceiling. Zero outside the Arrangement or on a dry part of a cell.
  [[nodiscard]] float getLiquidDepth(wp::Vector2 const& position) const;

  // Flat-world compatibility view, parallel to Arrangement faces. New code
  // must query depth by position or consume Pool elevations instead.
  [[nodiscard]] std::vector<float> const& getLiquidDepths() const;

  // The highest Liquid elevation reachable at position, derived from the
  // same locally floor/ceiling-clamped column as getLiquidDepth. Negative
  // infinity outside the Arrangement or on a dry part of a cell. In a
  // completely flooded cell this is the local ceiling; the Pool's unclamped
  // equilibrium remains available through getLiquidPoolElevations().
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
      wp::Vector2 const& destinationPosition,
      float radius,
      wp::Vector2 const& sourcePosition,
      bool descending = false) const;

  // True when authored collision or local standing clearance blocks this
  // wall independently of floor-step height. The supplied position is
  // projected onto the wall, so overlap recovery uses the same local surface
  // samples as traversal.
  [[nodiscard]] bool wallBlocksTraversalWithoutStepAt(
      uint32_t wallIndex,
      wp::Vector2 const& position) const;

  [[nodiscard]] int32_t circleIntersectsWall(
      wp::Vector2 const& position,
      float radius) const;

  // Destination-aware form used by actions such as Liquid mantling. Unlike
  // circleIntersectsWall's conservative collision-grid query, local Step
  // height and clearance are evaluated at the attempted crossing.
  [[nodiscard]] int32_t circleIntersectsWallForTraversal(
      wp::Vector2 const& destinationPosition,
      float radius,
      wp::Vector2 const& sourcePosition,
      bool descending = false) const;

  // How far a horizontal ray at `height` gets from `from` toward `to` before
  // the nearest rendered wall blocks it. Empty when it reaches `to` in the
  // clear. A wall blocks only over its evaluated span at the crossing, so the
  // ray passes above a low FloorStep and below a high CeilingStep exactly as
  // light leaving that height does, and a wall the World does not draw blocks
  // nothing. Collision is a separate question - see getWallsNear.
  [[nodiscard]] std::optional<float> distanceToFirstWallCrossing(
      wp::Vector2 const& from,
      wp::Vector2 const& to,
      float height) const;
};

using ArrangementWorldDataPtr = std::shared_ptr<ArrangementWorldData const>;
}  // namespace bw::core
