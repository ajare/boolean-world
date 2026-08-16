#pragma once

#include <vector>
#include <set>
#include <array>
#include <map>

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>
#include <willpower/common/BoundingCircle.h>
#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/ExtendedAccelerationGrid.h>

#include "core/Platform.h"
#include "core/Serializable.h"
#include "core/Primitive.h"
#include "core/Clipper.h"
#include "core/WorldData.h"
#include "core/WorldDataGenerator.h"
#include "core/Defines.h"
#include "core/PrefabAreaTilingType.h"
#include "core/WorldUpdateData.h"
#include "core/WorldTriggerLine.h"

namespace bw {
namespace core {

class BW_API World : public Serializable {
  struct SortPrimitivesByPriority {
    bool operator()(Primitive const* a, Primitive const* b) {
      return a->getPriority() < b->getPriority();
    }
  };

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

  wp::Vector2 mPlayerStartPosition;

  float mPlayerStartAngle;

  bool mAlwaysUpdateVertices;

  // Runtime
  frame_number_type mFrameNumber;

  std::function<void(PrimitiveCellMetadata*)> mPrimitiveCellMetadataUpdater;

  WorldDataGenerator* mDataGenerator;

  // Lookups / caching
  PrimitiveAccelerationGrid* mPrimitiveLookupGrid;

  wp::AccelerationGrid* mTriggerLookupGrid;

  mutable frame_number_type mLastPrimitiveUpdateFrameNumber;

  mutable frame_number_type mCachedVertexDataFrameNumber;

  mutable std::vector<WorldVertexData> mCachedBorderVertexData;

  wp::Vector2 mPrevPlayerPosition;

  // Metadata
  PrefabAreaTilingType mPrefabAreaTilingType;

  uint32_t mPrefabAreaTileTypes;

private:
  bool childrenModified() const override;

  Primitive* instantiatePrimitive(std::string const& type) const;

  std::vector<Primitive*> sortPrimitiveIndicesByPriority(std::vector<uint32_t> const& indices) const;

  WorldDataClipResults calculatePolygons(std::vector<Primitive*> const& primitives, uint32_t flags = 0) const;

  void validateVertexCount() const;

  void addPrimitiveToLookupGrid(Primitive* primitive);

  void removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound = true);

  void addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine);

  void removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine);

  void updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata);

  void handleEvents(uint32_t events);

protected:
  void copyFrom(World const& other);

  void preSerialization(SerializationWorkData& workData) const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  World();

  World(float size, float gridSize, WorldDataGeneratorFactory generatorFactory = nullptr);

  World(World const& other);

  World& operator=(World const& other);

  virtual ~World();

  void setWorldDataGeneratorFactory(WorldDataGeneratorFactory generatorFactory);

  WorldDataGenerator* getWorldDataGenerator();

  WorldDataGenerator const* getWorldDataGenerator() const;

  void createAccelerationGrids(float targetCellSize);

  float getPrimitiveAccelerationGridSize() const;

  void clear();

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

  [[nodiscard]] frame_number_type getFrameNumber() const;

  [[nodiscard]] wp::BoundingBox const& getExtents() const;

  void setPrefabAreaTilingType(PrefabAreaTilingType type);

  [[nodiscard]] PrefabAreaTilingType getPrefabAreaTilingType() const;

  void setPrefabAreaTileTypes(uint32_t types);

  [[nodiscard]] uint32_t getPrefabAreaTileTypes() const;

  bool getGridSettings(int* dimX, int* dimY, float* cellSize);

  void _cacheWorldVertexData() const;

  void _cachePrimitiveStaticness(bool cache);

  [[nodiscard]] std::vector<WorldVertexData> const& getBorderVertexData(frame_number_type* frameNumber = nullptr) const;

  uint32_t addPrimitive(Primitive* primitive);

  void removePrimitive(Primitive* primitive, bool failIfNotFound = true);

  void removePrimitive(uint32_t index);

  void removePrimitives(std::vector<uint32_t> const& indices);

  void replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound = true);

  void removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound = true);

  void removeTriggerLine(uint32_t index);

  void removeTriggerLines(std::vector<uint32_t> const& indices);

  void replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound = true);

  Primitive* createMeshPrimitive(std::vector<uint32_t> const& indices) const;

  uint32_t convertPrimitivesToMesh(std::vector<uint32_t> const& indices);

  void primitiveChanged(Primitive const* primitive);

  void getGridCellPrimitivesVersion(uint32_t cellIndex, frame_number_type* primitivesVersion) const;

  void getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const;

  [[nodiscard]] Primitive* getPrimitive(uint32_t index);

  [[nodiscard]] Primitive const* getPrimitive(uint32_t index) const;

  [[nodiscard]] std::vector<Primitive*> getPrimitivesInGridCell(uint32_t cellIndex, uint8_t activeLayer, frame_number_type* primitivesVersion = nullptr) const;

  [[nodiscard]] Primitive* findPrimitive(wp::Vector2 const& worldPos) const;

  [[nodiscard]] uint32_t findPrimitiveIndex(wp::Vector2 const& worldPos, bool exact, std::set<uint32_t> ignoreIndices = {}) const;

  [[nodiscard]] std::vector<uint32_t> findPrimitiveIndices(wp::Vector2 const& worldPos, bool exact, std::set<uint32_t> ignoreIndices = {}) const;

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

  [[nodiscard("returned WorldData should be used, otherwise method does nothing")]] WorldData getWorldData(wp::Vector2 const& position, float angle) const;
};

}  // namespace core
}  // namespace bw