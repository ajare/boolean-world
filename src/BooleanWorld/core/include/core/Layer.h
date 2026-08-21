#pragma once

#include <string>
#include <vector>
#include <functional>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/BoundingCircle.h>
#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/ExtendedAccelerationGrid.h>

#include "core/Platform.h"
#include "core/Serializable.h"
#include "core/Primitive.h"
#include "core/WorldTriggerLine.h"

namespace bw {
namespace core {

class LayerBuildContext;
class LayerBuildStep;
class PrimitiveField;
class World;

// A named collection of WorldTriggerLines and of the Primitives its ordered
// LayerBuildSteps produce. A World holds an ordered set of Layers; a
// generation selects a set of Layers by id and folds across their combined
// content (docs/adr/0013).
//
// A Layer's Primitives are derived, never stored: they are recomputed from
// scratch by re-running the enabled steps in order, and the step list - not
// the Primitives - is what a Layer serializes (docs/adr/0014). The
// addPrimitive/removePrimitive/replacePrimitive facade is preserved for
// authoring: addPrimitive writes into the active step (see setActiveStep;
// it starts out as the first step, permanently a PrimitiveField), while
// removePrimitive/replacePrimitive look the Primitive up by identity across
// every owning step regardless of which is active.
class BW_API Layer : public Serializable {
  friend class World;

  struct PrimitiveCellMetadata {
    frame_number_type lastUpdatedFrameNumber{0};
  };

  typedef wp::ExtendedAccelerationGrid<PrimitiveCellMetadata> PrimitiveAccelerationGrid;

private:
  // Non-owning backlink used to bind every Primitive produced by a recipe
  // rebuild. Null only while a Layer exists outside a World.
  World* mWorld;

  uint32_t mId;
  uint32_t mNextStepId;

  std::string mName;

  wp::BoundingBox mExtents;

  // The recipe. Index 0 is always a PrimitiveField step: it can be disabled,
  // but never deleted or retyped, and no step may be inserted before it.
  std::vector<LayerBuildStep*> mSteps;

  // The editor's authoring focus among mSteps: addPrimitive writes into this
  // step. Not part of the serialized Layer state, mirroring World's
  // mActiveLayerIndex (docs/adr/0013) - always 0 after construction, copy,
  // or load.
  uint32_t mActiveStepIndex;

  // Derived from mSteps, and not owned - every Primitive here belongs to the
  // step that produced it. mPrimitiveSteps is kept in lockstep with this
  // collection so ownership and capabilities do not depend on a step's type.
  std::vector<Primitive*> mPrimitives;
  std::vector<LayerBuildStep const*> mPrimitiveSteps;

  std::vector<WorldTriggerLine*> mTriggerLines;

  PrimitiveAccelerationGrid* mPrimitiveLookupGrid;

  wp::AccelerationGrid* mTriggerLookupGrid;

  // Stamped onto grid cells whose contents change. Pushed down by the owning
  // World as it advances; a Layer has no clock of its own.
  frame_number_type mFrameNumber;

  std::function<void(PrimitiveCellMetadata*)> mPrimitiveCellMetadataUpdater;

private:
  bool childrenModified() const override;

  // Drops the derived Primitives and everything the Layer owns outright.
  // Unlike clear(), it leaves no first step behind, so only teardown paths
  // may call it.
  void teardown();

  void seedFirstStep();

  void bindWorld(World* world);

  void assignStepId(LayerBuildStep* step);

  void deleteSteps();

  // The step that owns primitive, or null if no step does. This locates the
  // storage implementation for removal and replacement; it is not a
  // capability check (docs/adr/0015).
  [[nodiscard]] LayerBuildStep* findOwningStep(Primitive const* primitive) const;

  // True when no enabled step follows step, so what step contributes lands
  // at the end of the derived Primitives and appending to it cannot change
  // any other step's output - the case where the cache can be updated in
  // place instead of rebuilt.
  [[nodiscard]] bool isLastEnabledStep(LayerBuildStep const* step) const;

  // Whether any output of step forms one contiguous tail of the derived
  // cache. The in-place add and replace fast paths rely on this invariant.
  [[nodiscard]] bool hasContiguousTailOutput(LayerBuildStep const* step) const;

  void addPrimitiveToLookupGrid(Primitive* primitive);

  void removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound = true);

  void addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine);

  void removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine);

  void updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata);

  void swapState(Layer& other) noexcept;

  void rebindOwnedState();

  friend class LayerBuildContext;

protected:
  void copyFrom(Layer const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  Layer();

  Layer(uint32_t id, std::string const& name, float size, float gridSize);

  Layer(Layer const& other);

  Layer& operator=(Layer const& other);

  ~Layer() override;

  void createAccelerationGrids(float targetCellSize);

  // Re-homes the acceleration grids after the Layer's extents change: a
  // positive cell size rebuilds them from scratch, otherwise the existing
  // grids are kept and emptied.
  void rebuildAccelerationGrids(float targetCellSize);

  [[nodiscard]] float getPrimitiveAccelerationGridSize() const;

  bool getGridSettings(int* dimX, int* dimY, float* cellSize) const;

  void getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const;

  void _setFrameNumber(frame_number_type frameNumber);

  void clear();

  [[nodiscard]] uint32_t getId() const;

  // Rehomes a Layer under a new stable id - used when importing a
  // standalone Layer whose id collides with one a World already has.
  void _setId(uint32_t id);

  void setName(std::string const& name);

  [[nodiscard]] std::string const& getName() const;

  void setExtents(wp::BoundingBox const& extents);

  [[nodiscard]] wp::BoundingBox const& getExtents() const;

  // --- Build steps (docs/adr/0014) ---

  // Recomputes the derived Primitives from scratch by re-running the enabled
  // steps in order.
  void rebuild();

  [[nodiscard]] uint32_t getNumSteps() const;

  [[nodiscard]] LayerBuildStep* getStep(uint32_t index) const;
  [[nodiscard]] LayerBuildStep* getStepById(uint32_t id) const;

  // The first step, always a PrimitiveField. Distinct from the active step
  // below, which is where addPrimitive actually writes.
  [[nodiscard]] PrimitiveField* getPrimitiveField() const;

  [[nodiscard]] uint32_t getActiveStepIndex() const;

  [[nodiscard]] LayerBuildStep* getActiveStep() const;

  // Sets which step addPrimitive writes into. Rejected if index is out of
  // bounds; unlike World::setActiveLayer, any step type is accepted here.
  void setActiveStep(uint32_t index);

  // The index of the build step that produced primitive, or ~0u if no step
  // here did. This works for every step type.
  [[nodiscard]] uint32_t getOwningStepIndex(Primitive const* primitive) const;

  // Takes ownership of step and rebuilds. Index 0 is reserved for the
  // Layer's PrimitiveField step, so index must be >= 1; anything lower is
  // rejected and step is not adopted.
  uint32_t insertStep(uint32_t index, LayerBuildStep* step);

  uint32_t addStep(LayerBuildStep* step);

  // Rejected for index 0: the first step can be disabled, but never deleted
  // and never retyped.
  void removeStep(uint32_t index);

  // Rejected if either index is 0: index 0's step can never move, and no
  // other step may move into it. Rebuilds only when the enabled steps'
  // relative order actually changed.
  void moveStep(uint32_t fromIndex, uint32_t toIndex);

  void setStepEnabled(uint32_t index, bool enabled);

  // Appends a Primitive a step just produced to the derived collection.
  // Called by LayerBuildStep::execute during a rebuild; the Layer does not
  // take ownership. owningStep records the step's capabilities alongside its
  // derived Primitive.
  uint32_t _appendBuiltPrimitive(Primitive* primitive, LayerBuildStep const* owningStep);

  // Adds primitive to this Layer's active step and rebuilds, taking
  // ownership of it. Rejected when that step does not accept new Primitives
  // or is disabled - the Primitive would be authored into a recipe that
  // produces nothing.
  uint32_t addPrimitive(Primitive* primitive);

  void removePrimitive(Primitive* primitive, bool failIfNotFound = true);

  void removePrimitive(uint32_t index);

  void removePrimitives(std::vector<uint32_t> const& indices);

  void replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound = true);

  void primitiveChanged(Primitive const* primitive);

  [[nodiscard]] std::vector<uint32_t> getPrimitiveIndicesInGridCell(uint32_t cellIndex) const;

  // The Primitives worth testing against worldPos: the containing grid cell's
  // contents, or every Primitive when the position falls outside the grid.
  [[nodiscard]] std::vector<uint32_t> getPrimitiveCandidateIndices(wp::Vector2 const& worldPos) const;

  [[nodiscard]] uint32_t getNumPrimitives() const;

  [[nodiscard]] std::vector<Primitive*> const& getPrimitives() const;

  [[nodiscard]] Primitive* getPrimitive(uint32_t index) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingBox const& bounds) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingCircle const& bounds) const;

  uint32_t addTriggerLine(WorldTriggerLine* triggerLine);

  void removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound = true);

  void removeTriggerLine(uint32_t index);

  void removeTriggerLines(std::vector<uint32_t> const& indices);

  void replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound = true);

  void setTriggerLinePoints(uint32_t triggerLineIndex, wp::Vector2 const& p0, wp::Vector2 const& p1);

  [[nodiscard]] uint32_t getNumTriggerLines() const;

  [[nodiscard]] std::vector<WorldTriggerLine*> const& getTriggerLines() const;

  // The mutable collection Primitives bind to as a transform input.
  [[nodiscard]] std::vector<WorldTriggerLine*>* _getTriggerLineStorage();

  [[nodiscard]] WorldTriggerLine* getTriggerLine(uint32_t index) const;

  [[nodiscard]] std::vector<WorldTriggerLine*> findTriggerLines(wp::BoundingBox const& bounds) const;
};

}  // namespace core
}  // namespace bw
