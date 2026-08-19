#pragma once

#include <vector>
#include <set>
#include <array>
#include <functional>
#include <map>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/BoundingCircle.h>
#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/ExtendedAccelerationGrid.h>

#include "core/Platform.h"
#include "core/Serializable.h"
#include "core/Layer.h"
#include "core/Primitive.h"
#include "core/WorldData.h"
#include "core/WorldDataGenerator.h"
#include "core/Defines.h"
#include "core/PrefabAreaTilingType.h"
#include "core/WorldUpdateData.h"
#include "core/WorldTriggerLine.h"

namespace bw {
namespace core {

class BW_API World : public Serializable {
  struct PrimitiveCellMetadata {
    frame_number_type lastUpdatedFrameNumber{0};
  };

  typedef wp::ExtendedAccelerationGrid<PrimitiveCellMetadata> PrimitiveAccelerationGrid;

private:
  // Properties
  std::string mName;

  std::string mDescription;

  wp::BoundingBox mExtents;

  std::vector<Primitive*> mPrimitives;

  std::vector<WorldTriggerLine*> mTriggerLines;

  // The ordered set of Layers this World owns (docs/adr/0013). Never empty.
  // mActiveLayerIndex is the editor's current authoring focus, not part of
  // the serialized World state: it is always 0 after construction or load.
  std::vector<Layer*> mLayers;

  uint32_t mActiveLayerIndex;

  // Next id to hand to a Layer created via addLayer(). Ids are never reused,
  // so a Layer's identity survives removal/reordering of its siblings.
  uint32_t mNextLayerId;

  wp::Vector2 mPlayerStartPosition;

  float mPlayerStartAngle;

  bool mAlwaysUpdateVertices;

  float mStepThreshold;

  // Runtime
  frame_number_type mFrameNumber;

  std::function<void(PrimitiveCellMetadata*)> mPrimitiveCellMetadataUpdater;

  WorldDataGenerator* mDataGenerator;

  // Lookups / caching
  PrimitiveAccelerationGrid* mPrimitiveLookupGrid;

  wp::AccelerationGrid* mTriggerLookupGrid;

  mutable frame_number_type mLastPrimitiveUpdateFrameNumber;

  wp::Vector2 mPrevPlayerPosition;

  // Metadata
  PrefabAreaTilingType mPrefabAreaTilingType;

  uint32_t mPrefabAreaTileTypes;

private:
  bool childrenModified() const override;

  Primitive* instantiatePrimitive(std::string const& type) const;

  std::vector<Primitive*> sortPrimitiveIndicesByPriority(std::vector<uint32_t> const& indices) const;

  std::vector<uint32_t> getPrimitiveCandidateIndices(wp::Vector2 const& worldPos) const;

  void addPrimitiveToLookupGrid(Primitive* primitive);

  void removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound = true);

  void addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine);

  void removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine);

  void updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata);

  void swapState(World& other) noexcept;

  void rebindOwnedState();

  void handleEvents(uint32_t events);

protected:
  void copyFrom(World const& other);

  void preSerialization(SerializationWorkData& workData) const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  World();

  World(float size, float gridSize);

  World(World const& other);

  World& operator=(World const& other);

  virtual ~World();

  // Takes ownership of generator.
  void setWorldDataGenerator(WorldDataGenerator* generator);

  WorldDataGenerator* getWorldDataGenerator();

  WorldDataGenerator const* getWorldDataGenerator() const;

  void createAccelerationGrids(float targetCellSize);

  float getPrimitiveAccelerationGridSize() const;

  void clear();

  [[nodiscard]] std::vector<Layer*> const& getLayers() const;

  [[nodiscard]] uint32_t getNumLayers() const;

  [[nodiscard]] uint32_t getActiveLayerIndex() const;

  [[nodiscard]] Layer* getActiveLayer();

  [[nodiscard]] Layer const* getActiveLayer() const;

  Layer* addLayer(std::string const& name);

  void removeLayer(Layer* layer, bool failIfNotFound = true);

  void removeLayer(uint32_t index);

  void moveLayer(uint32_t fromIndex, uint32_t toIndex);

  void setName(std::string const& name);

  [[nodiscard]] std::string const& getName() const;

  void setDescription(std::string const& desc);

  [[nodiscard]] std::string const& getDescription() const;

  void setPlayerStartPosition(wp::Vector2 const& pos);

  [[nodiscard]] wp::Vector2 const& getPlayerStartPosition() const;

  void setPlayerStartAngle(float angle);

  [[nodiscard]] float getPlayerStartAngle() const;

  void setAlwaysUpdateVertices(bool always);

  [[nodiscard]] bool getAlwaysUpdateVertices() const;

  void setStepThreshold(float threshold);

  [[nodiscard]] float getStepThreshold() const;

  [[nodiscard]] frame_number_type getFrameNumber() const;

  [[nodiscard]] wp::BoundingBox const& getExtents() const;

  void setPrefabAreaTilingType(PrefabAreaTilingType type);

  [[nodiscard]] PrefabAreaTilingType getPrefabAreaTilingType() const;

  void setPrefabAreaTileTypes(uint32_t types);

  [[nodiscard]] uint32_t getPrefabAreaTileTypes() const;

  bool getGridSettings(int* dimX, int* dimY, float* cellSize);

  void _cachePrimitiveStaticness(bool cache);

  uint32_t addPrimitive(Primitive* primitive);

  void removePrimitive(Primitive* primitive, bool failIfNotFound = true);

  void removePrimitive(uint32_t index);

  void removePrimitives(std::vector<uint32_t> const& indices);

  void replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound = true);

  void removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound = true);

  void removeTriggerLine(uint32_t index);

  void removeTriggerLines(std::vector<uint32_t> const& indices);

  void replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound = true);

  void setTriggerLinePoint(uint32_t triggerLineIndex, uint32_t pointIndex, wp::Vector2 const& position);

  void setTriggerLinePoints(uint32_t triggerLineIndex, wp::Vector2 const& p0, wp::Vector2 const& p1);

  void moveTriggerLine(uint32_t triggerLineIndex, wp::Vector2 const& offset);

  Primitive* createMeshPrimitive(
      std::vector<Primitive*> const& fold) const;

  Primitive* createMeshPrimitive(std::vector<uint32_t> const& indices) const;

  uint32_t convertPrimitivesToMesh(std::vector<uint32_t> const& indices);

  void primitiveChanged(Primitive const* primitive);

  void getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const;

  [[nodiscard]] Primitive* getPrimitive(uint32_t index);

  [[nodiscard]] Primitive const* getPrimitive(uint32_t index) const;

  [[nodiscard]] std::vector<Primitive*> getPrimitivesInGridCell(uint32_t cellIndex, uint8_t activeLayer) const;

  [[nodiscard]] Primitive* findPrimitive(wp::Vector2 const& worldPos) const;

  [[nodiscard]] uint32_t findPrimitiveIndex(wp::Vector2 const& worldPos, bool exact, std::set<uint32_t> const& ignoreIndices = {}) const;

  [[nodiscard]] std::vector<uint32_t> findPrimitiveIndices(wp::Vector2 const& worldPos, bool exact, std::set<uint32_t> const& ignoreIndices = {}) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingBox const& bounds) const;

  [[nodiscard]] std::vector<Primitive*> findPrimitives(wp::BoundingCircle const& bounds) const;

  [[nodiscard]] uint32_t getNumPrimitives() const;

  [[nodiscard]] std::vector<Primitive*> const& getPrimitives() const;

  [[nodiscard]] std::vector<Primitive*> getPrimitivesByPriority() const;

  [[nodiscard]] uint32_t getNumTriggerLines() const;

  [[nodiscard]] std::vector<WorldTriggerLine*> const& getTriggerLines() const;

  [[nodiscard]] uint32_t findTriggerLineIndex(wp::Vector2 const& worldPos, float tolerance, float handleRadius = 0.0f) const;

  [[nodiscard]] WorldTriggerLine* getTriggerLine(uint32_t index) const;

  uint32_t addTriggerLine(WorldTriggerLine* triggerLine);

  [[nodiscard]] std::vector<WorldTriggerLine*> findTriggerLines(wp::BoundingBox const& bounds) const;

  void update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize);

  void generateClipping(bool regetPrimitives);

  [[nodiscard]] WorldDataPtr getWorldData() const;
};

}  // namespace core
}  // namespace bw