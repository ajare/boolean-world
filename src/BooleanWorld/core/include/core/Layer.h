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

// A named, owned collection of Primitives and WorldTriggerLines. A World
// holds an ordered set of Layers; a generation selects a set of Layers by
// id and folds across their combined content (docs/adr/0013).
class BW_API Layer : public Serializable {
  struct PrimitiveCellMetadata {
    frame_number_type lastUpdatedFrameNumber{0};
  };

  typedef wp::ExtendedAccelerationGrid<PrimitiveCellMetadata> PrimitiveAccelerationGrid;

private:
  uint32_t mId;

  std::string mName;

  wp::BoundingBox mExtents;

  std::vector<Primitive*> mPrimitives;

  std::vector<WorldTriggerLine*> mTriggerLines;

  PrimitiveAccelerationGrid* mPrimitiveLookupGrid;

  wp::AccelerationGrid* mTriggerLookupGrid;

  // Stamped onto grid cells whose contents change. Pushed down by the owning
  // World as it advances; a Layer has no clock of its own.
  frame_number_type mFrameNumber;

  std::function<void(PrimitiveCellMetadata*)> mPrimitiveCellMetadataUpdater;

private:
  bool childrenModified() const override;

  Primitive* instantiatePrimitive(std::string const& type) const;

  void addPrimitiveToLookupGrid(Primitive* primitive);

  void removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound = true);

  void addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine);

  void removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine);

  void updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata);

  void swapState(Layer& other) noexcept;

  void rebindOwnedState();

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

  void setName(std::string const& name);

  [[nodiscard]] std::string const& getName() const;

  void setExtents(wp::BoundingBox const& extents);

  [[nodiscard]] wp::BoundingBox const& getExtents() const;

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
