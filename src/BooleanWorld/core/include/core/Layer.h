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

  void clear();

  [[nodiscard]] uint32_t getId() const;

  void setName(std::string const& name);

  [[nodiscard]] std::string const& getName() const;

  [[nodiscard]] wp::BoundingBox const& getExtents() const;

  uint32_t addPrimitive(Primitive* primitive);

  void removePrimitive(Primitive* primitive, bool failIfNotFound = true);

  [[nodiscard]] uint32_t getNumPrimitives() const;

  [[nodiscard]] std::vector<Primitive*> const& getPrimitives() const;

  [[nodiscard]] Primitive* getPrimitive(uint32_t index) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingBox const& bounds) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingCircle const& bounds) const;

  uint32_t addTriggerLine(WorldTriggerLine* triggerLine);

  void removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound = true);

  [[nodiscard]] uint32_t getNumTriggerLines() const;

  [[nodiscard]] std::vector<WorldTriggerLine*> const& getTriggerLines() const;

  [[nodiscard]] WorldTriggerLine* getTriggerLine(uint32_t index) const;

  [[nodiscard]] std::vector<WorldTriggerLine*> findTriggerLines(wp::BoundingBox const& bounds) const;
};

}  // namespace core
}  // namespace bw
