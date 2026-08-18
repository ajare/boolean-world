#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <functional>
#include <cassert>

#include <mapbox/earcut.hpp>

#include <willpower/common/BezierSpline.h>
#include <willpower/common/MathsUtils.h>

#include "core/World.h"
#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/RectanglePolygon.h"
#include "core/RegularPolygon.h"
#include "core/CirclePolygon.h"
#include "core/CircleSegmentPolygon.h"
#include "core/TorusPolygon.h"
#include "core/TorusSegmentPolygon.h"
#include "core/SuperformulaPolygon.h"
#include "core/MeshPrimitive.h"
#include "core/ArrangementWorldDataGenerator.h"
#include "core/DefaultWorldDataGenerator.h"

#include "common/GameDefines.h"

namespace bw {
namespace core {

using namespace std;

World::World()
    : World(BW_WORLD_SIZE, -1.0f) {
}

World::World(float size, float gridSize)
    : mExtents(-size / 2, -size / 2, size, size), mPlayerStartPosition{0.0f, 0.0f}, mPlayerStartAngle(0.0f), mAlwaysUpdateVertices(false), mStepThreshold(numeric_limits<float>::infinity()), mFrameNumber(0), mDataGenerator(new DefaultWorldDataGenerator()), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr), mPrevPlayerPosition{999999.0f, 999999.0f}, mPrefabAreaTilingType(PrefabAreaTilingType::None), mPrefabAreaTileTypes(0), mLastPrimitiveUpdateFrameNumber(0) {
  mPrimitiveCellMetadataUpdater = bind(&World::updatePrimitiveCellMetadata, this, placeholders::_1);

  if (gridSize > 0.0f) {
    createAccelerationGrids(gridSize);
  }
}

World::World(World const& other)
    : mDataGenerator(nullptr), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr) {
  mPrimitiveCellMetadataUpdater = bind(&World::updatePrimitiveCellMetadata, this, placeholders::_1);

  copyFrom(other);
}

World& World::operator=(World const& other) {
  if (this == &other) {
    return *this;
  }

  World replacement(other);
  swapState(replacement);
  rebindOwnedState();
  replacement.rebindOwnedState();
  return *this;
}

void World::swapState(World& other) noexcept {
  Serializable::swapState(other);

  using std::swap;
  swap(mName, other.mName);
  swap(mDescription, other.mDescription);
  swap(mExtents, other.mExtents);
  swap(mPrimitives, other.mPrimitives);
  swap(mTriggerLines, other.mTriggerLines);
  swap(mPlayerStartPosition, other.mPlayerStartPosition);
  swap(mPlayerStartAngle, other.mPlayerStartAngle);
  swap(mAlwaysUpdateVertices, other.mAlwaysUpdateVertices);
  swap(mStepThreshold, other.mStepThreshold);
  swap(mFrameNumber, other.mFrameNumber);
  swap(mDataGenerator, other.mDataGenerator);
  swap(mPrimitiveLookupGrid, other.mPrimitiveLookupGrid);
  swap(mTriggerLookupGrid, other.mTriggerLookupGrid);
  swap(mLastPrimitiveUpdateFrameNumber, other.mLastPrimitiveUpdateFrameNumber);
  swap(mPrevPlayerPosition, other.mPrevPlayerPosition);
  swap(mPrefabAreaTilingType, other.mPrefabAreaTilingType);
  swap(mPrefabAreaTileTypes, other.mPrefabAreaTileTypes);
}

void World::rebindOwnedState() {
  for (auto* primitive : mPrimitives) {
    primitive->mWorld = this;
    primitive->mInputs.triggerLines = &mTriggerLines;
  }

  if (mDataGenerator) {
    mDataGenerator->rebindToWorld(this);
  }
}

World::~World() {
  clear();
}

void World::copyFrom(World const& other) {
  Serializable::copyFrom(other);

  mName = other.mName;
  mDescription = other.mDescription;
  mExtents = other.mExtents;
  mPlayerStartPosition = other.mPlayerStartPosition;
  mPlayerStartAngle = other.mPlayerStartAngle;
  mAlwaysUpdateVertices = other.mAlwaysUpdateVertices;
  mStepThreshold = other.mStepThreshold;
  mFrameNumber = other.mFrameNumber;
  mPrevPlayerPosition = other.mPrevPlayerPosition;
  mPrefabAreaTilingType = other.mPrefabAreaTilingType;
  mPrefabAreaTileTypes = other.mPrefabAreaTileTypes;
  mLastPrimitiveUpdateFrameNumber = other.mLastPrimitiveUpdateFrameNumber;

  // Add Acceleration grids
  if (other.mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid = new PrimitiveAccelerationGrid(
        other.mPrimitiveLookupGrid->getOffset(),
        other.mPrimitiveLookupGrid->getSize(),
        other.mPrimitiveLookupGrid->getCellDimensionX(),
        other.mPrimitiveLookupGrid->getCellDimensionY(),
        0.0f);
  } else {
    mPrimitiveLookupGrid = nullptr;
  }
  if (other.mTriggerLookupGrid) {
    mTriggerLookupGrid = new wp::AccelerationGrid(
        other.mTriggerLookupGrid->getOffset(),
        other.mTriggerLookupGrid->getSize(),
        other.mTriggerLookupGrid->getCellDimensionX(),
        other.mTriggerLookupGrid->getCellDimensionY(),
        0.0f);
  } else {
    mTriggerLookupGrid = nullptr;
  }

  // Trigger lines must exist before primitives so copied transform inputs can
  // refer to this World's collection immediately.
  for (auto triggerLine : other.mTriggerLines) {
    auto tl = make_unique<WorldTriggerLine>(*triggerLine);
    addTriggerLine(tl.get());
    tl.release();
  }

  // Clone every primitive before adding any of them. This lets parent links be
  // remapped as a complete graph, regardless of primitive ordering.
  map<VertexTransformerObject const*, VertexTransformerObject*> primitiveMap;
  vector<unique_ptr<Primitive>> primitives;
  primitives.reserve(other.mPrimitives.size());
  for (auto primitive : other.mPrimitives) {
    auto p = unique_ptr<Primitive>(primitive->copy());
    p->mWorld = nullptr;
    p->mInputs.triggerLines = &mTriggerLines;
    primitiveMap[primitive] = p.get();
    primitives.push_back(move(p));
  }

  for (size_t i = 0; i < primitives.size(); ++i) {
    auto const* sourceParent = other.mPrimitives[i]->mParent;
    auto const parent = primitiveMap.find(sourceParent);
    primitives[i]->mParent = parent != primitiveMap.end() ? parent->second : nullptr;
  }

  for (auto& primitive : primitives) {
    addPrimitive(primitive.get());
    primitive.release();
  }

  // Data generator
  if (other.mDataGenerator) {
    mDataGenerator = other.mDataGenerator->copyForWorld(this);
  } else {
    mDataGenerator = nullptr;
  }
}

Primitive* World::instantiatePrimitive(string const& type) const {
  typedef function<Primitive*()> PrimitiveCreator;

  static const map<string, PrimitiveCreator> primCreators = {
      {"Rectangle", []() { return new RectanglePolygon; }},
      {"Regular", []() { return new RegularPolygon; }},
      {"Circle", []() { return new CirclePolygon; }},
      {"CircleSegment", []() { return new CircleSegmentPolygon; }},
      {"Torus", []() { return new TorusPolygon; }},
      {"TorusSegment", []() { return new TorusSegmentPolygon; }},
      {"Superformula", []() { return new SuperformulaPolygon; }},
      {"Mesh", []() { return new MeshPrimitive; }}};

  auto it = primCreators.find(type);
  if (it == primCreators.end()) {
    throw CoreException(format("No primitive of type '{}' registered", type));
  }

  return it->second();
}

bool World::childrenModified() const {
  for (auto const* primitive : mPrimitives) {
    if (primitive->isModified()) {
      return true;
    }
  }

  for (auto const* triggerLine : mTriggerLines) {
    if (triggerLine->isModified()) {
      return true;
    }
  }

  return false;
}

void World::preSerialization(SerializationWorkData& workData) const {
  BW_UNUSED(workData);
}

void World::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("root");
  {
    serializer->beginMap("world");
    {
      serializer->writeString("name", getName());
      serializer->writeString("description", getDescription());

      serializer->writeVector2("minExtent", mExtents.getMinExtent());
      serializer->writeVector2("maxExtent", mExtents.getMaxExtent());
      serializer->writeVector2("playerStartPosition", mPlayerStartPosition);
      serializer->writeFloat("playerStartAngle", mPlayerStartAngle);
      serializer->writeFloat("stepThreshold", mStepThreshold);

      serializer->beginMap("tiling");
      {
        serializer->writeUint32("prefabAreaTilingType", (uint32_t)mPrefabAreaTilingType);
        serializer->writeUint32("prefabAreaTileTypes", mPrefabAreaTileTypes);

        serializer->endMap();  // tiling
      }

      serializer->beginArray("primitives");
      {
        for (auto const* primitive : mPrimitives) {
          if (!workData.includeGhostPrimitives &&
              (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0) {
            continue;
          }

          // Serialize the type, so we can instantiate it ahead of time during deserialization.
          serializer->beginMap("primitive");
          {
            serializer->writeString("type", primitive->getType());

            primitive->serialize(serializer, workData);

            serializer->endMap();  // primitive
          }
        }

        serializer->endArray();  // primitives
      }

      serializer->beginArray("triggerLines");
      {
        for (auto const* triggerLine : mTriggerLines) {
          triggerLine->serialize(serializer, workData);
        }

        serializer->endArray();  // triggerlines
      }

      serializer->endMap();  // world
    }
  }

  serializer->endMap();  // root
}

bool World::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  workData.vtoIdToVtoMap.clear();
  workData.vtoIdToParentMap.clear();

  // Read in to temporary objects
  string worldName, description;
  wp::Vector2 minExtent, maxExtent;
  wp::Vector2 playerStartPosition;
  float playerStartAngle;
  float stepThreshold;
  PrefabAreaTilingType prefabAreaTilingType;
  uint32_t prefabAreaTileTypes;
  vector<unique_ptr<Primitive>> primitives;
  vector<unique_ptr<WorldTriggerLine>> triggerLines;

  uint32_t numVertices{0};

  try {
    serializer->beginMap("root");
    {
      serializer->beginMap("world");
      {
        worldName = serializer->readString("name");
        description = serializer->readString("description", true);
        minExtent = serializer->readVector2("minExtent");
        maxExtent = serializer->readVector2("maxExtent");
        playerStartPosition = serializer->readVector2("playerStartPosition");
        playerStartAngle = serializer->readFloat("playerStartAngle");
        stepThreshold = serializer->readFloat(
            "stepThreshold", true, numeric_limits<float>::infinity());

        serializer->beginMap("tiling");
        {
          prefabAreaTilingType = (PrefabAreaTilingType)serializer->readUint32("prefabAreaTilingType");
          prefabAreaTileTypes = serializer->readUint32("prefabAreaTileTypes");

          serializer->endMap();  // tiling
        }

        serializer->beginArray("primitives");
        {
          while (serializer->nextArrayItem()) {
            serializer->beginMap("primitive");
            {
              auto primitiveType = serializer->readString("type");

              // Instantiate primitive and deserialize
              auto primitive = unique_ptr<Primitive>(instantiatePrimitive(primitiveType));

              if (!primitive->deserialize(serializer, workData)) {
                copyErrorsAndWarnings(primitive.get(), true, true);
                return false;
              }

              numVertices += primitive->getNumVertices();

              if (numVertices > BW_VERTEX_COUNT_USEABLE_MAX) {
                throw CoreException("The World contains too many vertices");
              }

              primitives.push_back(move(primitive));

              serializer->endMap();  // primitive
            }
          }

          serializer->endArray();  // primitives
        }

        serializer->beginArray("triggerLines");
        {
          while (serializer->nextArrayItem()) {
            // Instantiate triggerline and deserialize
            auto triggerLine = make_unique<WorldTriggerLine>();

            if (!triggerLine->deserialize(serializer, workData)) {
              copyErrorsAndWarnings(triggerLine.get(), true, true);
              return false;
            }

            triggerLines.push_back(move(triggerLine));
          }

          serializer->endArray();  // triggerlines
        }
      }

      // Validate parent chains before linking the temporary primitives.
      map<uint32_t, uint8_t> parentVisitState;
      function<bool(uint32_t)> validateParentChain = [&](uint32_t id) {
        auto& state = parentVisitState[id];
        if (state == 1) {
          return false;
        }
        if (state == 2) {
          return true;
        }

        state = 1;
        auto parentIdIt = workData.vtoIdToParentMap.find(id);
        if (parentIdIt != workData.vtoIdToParentMap.end() && parentIdIt->second >= 0) {
          auto const parentId = uint32_t(parentIdIt->second);
          if (workData.vtoIdToVtoMap.contains(parentId) &&
              !validateParentChain(parentId)) {
            return false;
          }
        }
        state = 2;
        return true;
      };

      for (auto const& item : workData.vtoIdToVtoMap) {
        if (!validateParentChain(item.first)) {
          throw CoreException("Primitive parent chain contains a cycle");
        }
      }

      // Fix up VertexTransformer parents.
      for (auto const& [id, vt] : workData.vtoIdToVtoMap) {
        auto parentIdIt = workData.vtoIdToParentMap.find(id);
        if (parentIdIt == workData.vtoIdToParentMap.end() || parentIdIt->second < 0) {
          continue;
        }

        auto const parentId = uint32_t(parentIdIt->second);
        auto parentIt = workData.vtoIdToVtoMap.find(parentId);
        if (parentIt == workData.vtoIdToVtoMap.end()) {
          addDeserializationWarning(format("Unknown primitive parent id {}", parentId));
          continue;
        }
        vt->setParent(parentIt->second);
      }

      serializer->endMap();  // world
    }

    serializer->endMap();  // root
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  if (primitives.empty() && !workData.allowEmptyWorld) {
    addDeserializationError("World file contains no primitives!");

    return false;
  }

  // Commit
  mName = worldName;
  mDescription = description;
  mExtents.setPosition(minExtent);
  mExtents.setSize(maxExtent - minExtent);
  mPlayerStartPosition = playerStartPosition;
  mPlayerStartAngle = playerStartAngle;
  mStepThreshold = stepThreshold;
  mPrefabAreaTilingType = prefabAreaTilingType;
  mPrefabAreaTileTypes = prefabAreaTileTypes;

  // Create accel grids after setting extents but before adding primitives
  if (workData.accelGridSize > 0.0f) {
    delete mPrimitiveLookupGrid;
    mPrimitiveLookupGrid = nullptr;
    delete mTriggerLookupGrid;
    mTriggerLookupGrid = nullptr;

    createAccelerationGrids(workData.accelGridSize);
  } else {
    if (mPrimitiveLookupGrid) {
      mPrimitiveLookupGrid->removeAllItems(mPrimitiveCellMetadataUpdater);
    }

    if (mTriggerLookupGrid) {
      mTriggerLookupGrid->removeAllItems();
    }
  }

  // Add TriggerLines before Primitives, as we will need them to set
  // Primitive initial values
  for (auto& triggerLine : triggerLines) {
    addTriggerLine(triggerLine.get());
    triggerLine.release();
  }

  // Fix up Primitive indices.  Because we might be dealing with
  // ghost Primitives, a World might have been saved which does not
  // have sequential indices.  We need to make sure this is enforecd, though.
  // Invalidate each primitive so it recalculates its transformed vertices.
  for (auto& primitive : primitives) {
    auto* addedPrimitive = primitive.get();
    addPrimitive(addedPrimitive);
    primitive.release();
    addedPrimitive->_invalidate();
  }

  mFrameNumber = 0;
  mLastPrimitiveUpdateFrameNumber = 0;

  // Calculate vertices/bounds to initialise
  for (auto primitive : mPrimitives) {
    primitive->updateTime(0.0, {wp::Vector2::ZERO,
                                BW_PLAYER_RADIUS,
                                BW_PLAYER_FOV,
                                BW_PLAYER_VIEW_DISTANCE,
                                false,
                                false,
                                0});

    primitive->setInputs(wp::Vector2::ZERO, 0.0f, &mTriggerLines);
    primitive->calculateAnimationValues();
    primitive->updateVertexPositions();
    primitive->calculateBounds();
  }

  return true;
}

void World::setWorldDataGenerator(WorldDataGenerator* generator) {
  delete mDataGenerator;
  mDataGenerator = generator;
}

WorldDataGenerator* World::getWorldDataGenerator() {
  return mDataGenerator;
}

WorldDataGenerator const* World::getWorldDataGenerator() const {
  return mDataGenerator;
}

void World::createAccelerationGrids(float targetCellSize) {
  if ((mExtents.getSize().x / targetCellSize) > BW_PRIMITIVE_GRID_DIM_MAX) {
    throw CoreException(format("Grid sizes cannot be greater than {}.", BW_PRIMITIVE_GRID_DIM_MAX));
  }

  wp::Vector2 minExtent, maxExtent;
  mExtents.getExtents(minExtent, maxExtent);

  auto worldOffset = minExtent;
  auto worldSize = maxExtent - minExtent;

  int dimsX = max(1, (int)(worldSize.x / targetCellSize));
  int dimsY = max(1, (int)(worldSize.y / targetCellSize));

  if (!mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid = new PrimitiveAccelerationGrid(worldOffset, worldSize, dimsX, dimsY, 0.0f);
  }

  if (!mTriggerLookupGrid) {
    mTriggerLookupGrid = new wp::AccelerationGrid(worldOffset, worldSize, dimsX, dimsY, 0.0f);
  }
}

float World::getPrimitiveAccelerationGridSize() const {
  return mPrimitiveLookupGrid ? mPrimitiveLookupGrid->getCellSize().x : -1.0f;
}

void World::clear() {
  mName = "";
  mDescription = "";
  mFrameNumber = 0;
  mLastPrimitiveUpdateFrameNumber = 0;

  delete mDataGenerator;
  mDataGenerator = nullptr;

  delete mPrimitiveLookupGrid;
  mPrimitiveLookupGrid = nullptr;

  delete mTriggerLookupGrid;
  mTriggerLookupGrid = nullptr;

  for (auto primitive : mPrimitives) {
    delete primitive;
  }

  mPrimitives.clear();

  for (auto triggerLine : mTriggerLines) {
    delete triggerLine;
  }

  mTriggerLines.clear();
}

void World::setName(string const& name) {
  mName = name;
}

string const& World::getName() const {
  return mName;
}

void World::setDescription(string const& desc) {
  mDescription = desc;
}

string const& World::getDescription() const {
  return mDescription;
}

wp::BoundingBox const& World::getExtents() const {
  return mExtents;
}

void World::setPlayerStartPosition(wp::Vector2 const& pos) {
  mPlayerStartPosition = pos;
}

wp::Vector2 const& World::getPlayerStartPosition() const {
  return mPlayerStartPosition;
}

void World::setPlayerStartAngle(float angle) {
  mPlayerStartAngle = angle;
}

float World::getPlayerStartAngle() const {
  return mPlayerStartAngle;
}

void World::setAlwaysUpdateVertices(bool always) {
  mAlwaysUpdateVertices = always;
}

bool World::getAlwaysUpdateVertices() const {
  return mAlwaysUpdateVertices;
}

void World::setStepThreshold(float threshold) {
  mStepThreshold = threshold;
}

float World::getStepThreshold() const {
  return mStepThreshold;
}

void World::setPrefabAreaTilingType(PrefabAreaTilingType type) {
  mPrefabAreaTilingType = type;
}

PrefabAreaTilingType World::getPrefabAreaTilingType() const {
  return mPrefabAreaTilingType;
}

void World::setPrefabAreaTileTypes(uint32_t types) {
  mPrefabAreaTileTypes = types;
}

uint32_t World::getPrefabAreaTileTypes() const {
  return mPrefabAreaTileTypes;
}

frame_number_type World::getFrameNumber() const {
  return mFrameNumber;
}

bool World::getGridSettings(int* dimX, int* dimY, float* cellSize) {
  if (!mPrimitiveLookupGrid) {
    return false;
  }

  if (dimX) {
    *dimX = mPrimitiveLookupGrid->getCellDimensionX();
  }
  if (dimY) {
    *dimY = mPrimitiveLookupGrid->getCellDimensionY();
  }
  if (cellSize) {
    *cellSize = mPrimitiveLookupGrid->getCellSize().x;
  }

  return true;
}

void World::_cachePrimitiveStaticness(bool cache) {
  for (auto prim : mPrimitives) {
    prim->cacheStaticness(cache);
  }
}

void World::updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata) {
  metadata->lastUpdatedFrameNumber = max(metadata->lastUpdatedFrameNumber, mFrameNumber);
}

uint32_t World::addTriggerLine(WorldTriggerLine* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  auto index = (uint32_t)mTriggerLines.size();

  mTriggerLines.push_back(triggerLine);

  triggerLine->setId(index);
  addTriggerLineToLookupGrid(triggerLine);

  return index;
}

vector<WorldTriggerLine*> World::findTriggerLines(wp::BoundingBox const& bounds) const {
  vector<WorldTriggerLine*> result;

  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  wp::AccelerationGrid::IndexCollection indices;
  mTriggerLookupGrid->getCandidateItemsInBoundingArea(bounds, indices);

  for (auto index : indices) {
    auto triggerLine = mTriggerLines[index];

    result.push_back(triggerLine);
  }

  return result;
}

uint32_t World::addPrimitive(Primitive* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  if (getNumPrimitives() >= BW_WORLD_PRIMITIVE_COUNT_MAX) {
    throw CoreException("Too many primitives added to the World");
  }

  auto index = (uint32_t)mPrimitives.size();

  mPrimitives.push_back(primitive);

  primitive->setId(index);
  primitive->mWorld = this;

  addPrimitiveToLookupGrid(primitive);

  primitive->invalidatePostTransform(true, true);

  return index;
}

void World::removePrimitive(Primitive* primitive, bool failIfNotFound) {
  auto numPrimitives = (uint32_t)mPrimitives.size();
  for (uint32_t i = 0; i < numPrimitives; ++i) {
    if (mPrimitives[i] == primitive) {
      if (mPrimitiveLookupGrid) {
        mPrimitiveLookupGrid->removeItem(primitive->getId(), mPrimitiveCellMetadataUpdater);
      }

      delete primitive;

      for (uint32_t j = i; j < numPrimitives - 1; ++j) {
        removePrimitiveFromLookupGrid(mPrimitives[j + 1]);

        mPrimitives[j] = mPrimitives[j + 1];
        mPrimitives[j]->setId(j);

        addPrimitiveToLookupGrid(mPrimitives[j]);
      }

      mPrimitives.pop_back();
      return;
    }
  }

  if (failIfNotFound) {
    throw CoreException(format("{} primitive {} not found in world", primitive->getType(), primitive->getName()));
  }
}

void World::removePrimitive(uint32_t index) {
  assert(index < getNumPrimitives() && "World::removePrimitive(index) - index out of bounds");

  removePrimitive(mPrimitives[index]);
}

void World::removePrimitives(vector<uint32_t> const& indices) {
  auto sortedIndices = indices;
  sort(sortedIndices.begin(), sortedIndices.end());

  uint32_t tCount{0};
  uint32_t selectedIndex{0};
  auto numPrimitives = (uint32_t)mPrimitives.size();
  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto found = selectedIndex < sortedIndices.size() && sortedIndices[selectedIndex] == i;
    while (selectedIndex < sortedIndices.size() && sortedIndices[selectedIndex] <= i) {
      selectedIndex++;
    }
    if (!found) {
      if (i != tCount) {
        removePrimitiveFromLookupGrid(mPrimitives[i]);

        mPrimitives[tCount] = mPrimitives[i];
        mPrimitives[tCount]->setId(tCount);

        addPrimitiveToLookupGrid(mPrimitives[tCount]);
      }

      tCount++;
    } else {
      removePrimitiveFromLookupGrid(mPrimitives[i]);

      delete mPrimitives[i];
    }
  }

  auto numDeleted = numPrimitives - tCount;

  if (numDeleted != (uint32_t)indices.size()) {
    throw CoreException("Could not delete all selected primitives");
  }

  while (numDeleted > 0) {
    mPrimitives.pop_back();
    numDeleted--;
  }
}

void World::replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound) {
  assert(index < getNumPrimitives() && "World::replacePrimitive(index, newPrimitive) - index out of bounds");

  try {
    removePrimitiveFromLookupGrid(mPrimitives[index], failIfNotFound);
  } catch (exception const&) {
    if (failIfNotFound) {
      throw;
    }
  }

  auto oldPrimitive = mPrimitives[index];

  if (newPrimitive != oldPrimitive) {
    delete mPrimitives[index];
    mPrimitives[index] = newPrimitive;
  }

  newPrimitive->setId(index);
  newPrimitive->mWorld = this;
  addPrimitiveToLookupGrid(newPrimitive);
}

void World::removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound) {
  if (mTriggerLookupGrid) {
    mTriggerLookupGrid->removeItem(triggerLine->getId());
  }

  auto numTriggerLines = (uint32_t)mTriggerLines.size();
  for (uint32_t i = 0; i < numTriggerLines; ++i) {
    if (mTriggerLines[i] == triggerLine) {
      delete triggerLine;

      for (uint32_t j = i; j < numTriggerLines - 1; ++j) {
        removeTriggerLineFromLookupGrid(mTriggerLines[j + 1]);

        mTriggerLines[j] = mTriggerLines[j + 1];
        mTriggerLines[j]->setId(j);

        addTriggerLineToLookupGrid(mTriggerLines[j]);
      }

      mTriggerLines.pop_back();
      return;
    }
  }

  if (failIfNotFound) {
    throw CoreException("WorldTriggerLine not found.");
  }
}

void World::removeTriggerLine(uint32_t index) {
  assert(index < getNumTriggerLines() && "World::removeTriggerLine(index) - index out of bounds");

  removeTriggerLine(mTriggerLines[index]);
}

void World::removeTriggerLines(vector<uint32_t> const& indices) {
  uint32_t tCount{0};
  auto numTriggerLines = (uint32_t)mTriggerLines.size();
  for (uint32_t i = 0; i < numTriggerLines; ++i) {
    auto found = find(indices.begin(), indices.end(), i) != indices.end();
    if (!found) {
      if (i != tCount) {
        removeTriggerLineFromLookupGrid(mTriggerLines[i]);

        mTriggerLines[tCount] = mTriggerLines[i];
        mTriggerLines[tCount]->setId(tCount);

        addTriggerLineToLookupGrid(mTriggerLines[tCount]);
      }

      tCount++;
    } else {
      removeTriggerLineFromLookupGrid(mTriggerLines[i]);

      delete mTriggerLines[i];
    }
  }

  auto numDeleted = numTriggerLines - tCount;

  if (numDeleted != (uint32_t)indices.size()) {
    throw CoreException("Could not delete all selected triggerlines");
  }

  while (numDeleted > 0) {
    mTriggerLines.pop_back();
    numDeleted--;
  }
}

void World::replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound) {
  assert(index < getNumTriggerLines() && "World::replaceTriggerLine(index, newTriggerLine) - index out of bounds");

  try {
    removeTriggerLineFromLookupGrid(mTriggerLines[index]);
  } catch (exception const&) {
    if (failIfNotFound) {
      throw;
    }
  }

  auto oldTriggerLine = mTriggerLines[index];

  if (newTriggerLine != oldTriggerLine) {
    delete mTriggerLines[index];
    mTriggerLines[index] = newTriggerLine;
  }

  newTriggerLine->setId(index);
  addTriggerLineToLookupGrid(newTriggerLine);
}

void World::setTriggerLinePoint(uint32_t triggerLineIndex, uint32_t pointIndex, wp::Vector2 const& position) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::setTriggerLinePoint - trigger line index out of bounds");
  assert(pointIndex < 2 && "World::setTriggerLinePoint - point index out of bounds");

  auto* triggerLine = mTriggerLines[triggerLineIndex];
  auto p0 = triggerLine->getPoint(0);
  auto p1 = triggerLine->getPoint(1);
  (pointIndex == 0 ? p0 : p1) = position;
  setTriggerLinePoints(triggerLineIndex, p0, p1);
}

void World::setTriggerLinePoints(uint32_t triggerLineIndex, wp::Vector2 const& p0, wp::Vector2 const& p1) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::setTriggerLinePoints - trigger line index out of bounds");

  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  auto* triggerLine = mTriggerLines[triggerLineIndex];
  triggerLine->setPoints(p0, p1);
  mTriggerLookupGrid->moveItem(triggerLineIndex, triggerLine->getBounds());
}

void World::moveTriggerLine(uint32_t triggerLineIndex, wp::Vector2 const& offset) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::moveTriggerLine - trigger line index out of bounds");

  auto const* triggerLine = mTriggerLines[triggerLineIndex];
  setTriggerLinePoints(
      triggerLineIndex,
      triggerLine->getPoint(0) + offset,
      triggerLine->getPoint(1) + offset);
}

Primitive* World::createMeshPrimitive(vector<Primitive*> const& fold) const {
  auto selected = fold;
  stable_sort(
      selected.begin(), selected.end(),
      WorldDataGenerator::SortPrimitivesByPriority());
  ArrangementWorldDataGenerator generator;
  generator.generate(selected);
  auto arrangement = generator.getWorldData();

  auto boundaryVertices = [&](vector<uint32_t> const& vertexIndices) {
    ClosedPolygon polygon;
    for (auto vertexIndex : vertexIndices) {
      polygon.push_back(
          {{arr::ToWorldCoordinate(arrangement->vertices[vertexIndex].x),
            arr::ToWorldCoordinate(arrangement->vertices[vertexIndex].y)}});
    }
    return polygon;
  };

  vector<ComplexPolygon> polygons;
  for (auto const& face : arrangement->faces) {
    if (!face.solid || face.outerBoundary.empty()) {
      continue;
    }
    ComplexPolygon polygon;
    polygon.push_back(boundaryVertices(face.outerBoundaryVertices));
    for (auto const& hole : face.innerBoundaryVertices) {
      polygon.push_back(boundaryVertices(hole));
    }
    polygons.push_back(move(polygon));
  }
  if (polygons.empty()) {
    return nullptr;
  }

  auto mesh = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union,
      Primitive::FillRule::EvenOdd,
      move(polygons));
  uint8_t priority{0};
  for (auto primitive : fold) {
    priority = max(priority, primitive->getPriority());
  }
  mesh->setPriority(priority);
  return mesh;
}

Primitive* World::createMeshPrimitive(vector<uint32_t> const& indices) const {
  return createMeshPrimitive(sortPrimitiveIndicesByPriority(indices));
}

uint32_t World::convertPrimitivesToMesh(vector<uint32_t> const& indices) {
  auto mesh = createMeshPrimitive(indices);
  if (!mesh) {
    return ~0u;
  }
  removePrimitives(indices);
  return addPrimitive(mesh);
}

void World::primitiveChanged(Primitive const* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  mLastPrimitiveUpdateFrameNumber = mFrameNumber;

  auto id = primitive->getId();

  // Get the containing grid cell(s), and set the version
  auto const& cellIndices = mPrimitiveLookupGrid->_getItemCellIndices(id);

  for (auto cellIndex : cellIndices) {
    auto& userData = mPrimitiveLookupGrid->getUser(cellIndex);

    mPrimitiveCellMetadataUpdater(&userData);
  }

  // Move the item between grids as required
  mPrimitiveLookupGrid->moveItem(id, primitive->getBounds(), mPrimitiveCellMetadataUpdater);
}

Primitive* World::getPrimitive(uint32_t index) {
  assert(index < getNumPrimitives() && "World::getPrimitive(index) - index out of bounds");

  return mPrimitives[index];
}

Primitive const* World::getPrimitive(uint32_t index) const {
  assert(index < getNumPrimitives() && "World::getPrimitive(index) - index out of bounds");

  return mPrimitives[index];
}

uint32_t World::getNumPrimitives() const {
  return (uint32_t)mPrimitives.size();
}

vector<Primitive*> const& World::getPrimitives() const {
  return mPrimitives;
}

vector<Primitive*> World::getPrimitivesByPriority() const {
  auto sorted = mPrimitives;

  stable_sort(
      sorted.begin(), sorted.end(),
      WorldDataGenerator::SortPrimitivesByPriority());

  return sorted;
}

uint32_t World::getNumTriggerLines() const {
  return (uint32_t)mTriggerLines.size();
}

vector<WorldTriggerLine*> const& World::getTriggerLines() const {
  return mTriggerLines;
}

WorldTriggerLine* World::getTriggerLine(uint32_t index) const {
  return mTriggerLines[index];
}

uint32_t World::findTriggerLineIndex(wp::Vector2 const& worldPos, float tolerance, float handleRadius) const {
  auto numTriggerLines = getNumTriggerLines();
  auto handleRadiusSq = handleRadius * handleRadius;
  auto toleranceSq = tolerance * tolerance;

  for (uint32_t i = 0; i < numTriggerLines; ++i) {
    auto triggerLine = mTriggerLines[i];
    auto id = triggerLine->getId();

    auto const& p0 = triggerLine->getPoint(0);

    if (worldPos.distanceToSq(p0) < handleRadiusSq) {
      return id;
    }

    auto const& p1 = triggerLine->getPoint(1);

    if (worldPos.distanceToSq(p1) < handleRadiusSq) {
      return id;
    }

    if (worldPos.distanceToLineSq(p0, p1) < toleranceSq) {
      return id;
    }
  }

  return ~0u;
}

void World::getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  *frameNumber = mPrimitiveLookupGrid->getUser(cellIndex).lastUpdatedFrameNumber;
}

vector<Primitive*> World::getPrimitivesInGridCell(uint32_t cellIndex, uint8_t activeLayer) const {
  vector<Primitive*> result;

  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  auto const& primIndices = mPrimitiveLookupGrid->_getCellItemIndices(cellIndex);
  for (auto primIndex : primIndices) {
    auto prim = mPrimitives[primIndex];

    if (prim->getLayer() == activeLayer) {
      result.push_back(prim);
    }
  }

  return result;
}

Primitive* World::findPrimitive(wp::Vector2 const& worldPos) const {
  auto index = findPrimitiveIndex(worldPos, false);

  return index != ~0u ? mPrimitives[index] : nullptr;
}

vector<uint32_t> World::getPrimitiveCandidateIndices(wp::Vector2 const& worldPos) const {
  vector<uint32_t> result;
  if (mPrimitiveLookupGrid) {
    int cellX;
    int cellY;
    mPrimitiveLookupGrid->getContainingCell(true, worldPos.x, worldPos.y, cellX, cellY);
    if (cellX >= 0 && cellY >= 0) {
      auto const cellIndex = uint32_t(cellY * mPrimitiveLookupGrid->getCellDimensionX() + cellX);
      auto const& candidates = mPrimitiveLookupGrid->_getCellItemIndices(cellIndex);
      result.assign(candidates.begin(), candidates.end());
      return result;
    }
  }

  // Preserve picking for primitives and positions outside the configured grid.
  result.reserve(mPrimitives.size());
  for (uint32_t i = 0; i < uint32_t(mPrimitives.size()); ++i) {
    result.push_back(i);
  }
  return result;
}

uint32_t World::findPrimitiveIndex(wp::Vector2 const& worldPos, bool exact, set<uint32_t> const& ignoreIndices) const {
  auto const candidates = getPrimitiveCandidateIndices(worldPos);

  for (auto i : candidates) {
    auto primitive = mPrimitives[i];

    auto const& bounds = primitive->getBounds();
    if (bounds.pointInside(worldPos) && ignoreIndices.find(i) == ignoreIndices.end()) {
      if (exact) {
        if (primitive->getPickingTriangulation().pointInside(worldPos)) {
          return i;
        }
      } else {
        return i;
      }
    }
  }

  return ~0u;
}

vector<uint32_t> World::findPrimitiveIndices(wp::Vector2 const& worldPos, bool exact, set<uint32_t> const& ignoreIndices) const {
  vector<uint32_t> result;
  auto const candidates = getPrimitiveCandidateIndices(worldPos);

  for (auto i : candidates) {
    auto primitive = mPrimitives[i];

    if (!primitive->hasFlag(BW_PRIMITIVE_INTERACTS_FLAG)) {
      continue;
    }

    auto const& bounds = primitive->getBounds();
    if (bounds.pointInside(worldPos) && ignoreIndices.find(i) == ignoreIndices.end()) {
      if (exact) {
        if (primitive->getPickingTriangulation().pointInside(worldPos)) {
          result.push_back(i);
        }
      } else {
        result.push_back(i);
      }
    }
  }

  return result;
}

vector<Primitive*> World::findPrimitives(wp::BoundingBox const& bounds) const {
  vector<Primitive*> result;

  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  PrimitiveAccelerationGrid::IndexCollection indices;
  mPrimitiveLookupGrid->getCandidateItemsInBoundingArea(bounds, indices);

  for (auto index : indices) {
    auto primitive = mPrimitives[index];

    if (primitive->hasFlag(BW_PRIMITIVE_INTERACTS_FLAG)) {
      result.push_back(primitive);
    }
  }

  return result;
}

vector<Primitive*> World::findPrimitives(wp::BoundingCircle const& bounds) const {
  vector<Primitive*> result;

  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  PrimitiveAccelerationGrid::IndexCollection indices;
  mPrimitiveLookupGrid->getCandidateItemsInBoundingArea(bounds, indices);

  for (auto index : indices) {
    auto primitive = mPrimitives[index];

    if (primitive->hasFlag(BW_PRIMITIVE_INTERACTS_FLAG)) {
      result.push_back(primitive);
    }
  }

  return result;
}

void World::handleEvents(uint32_t /*events*/) {
}

void World::update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize) {
  mFrameNumber++;

  uint32_t globalEvents{0};

  if (mPrevPlayerPosition.x < 999998.0f) {
    auto sweptPlayerBounds = wp::BoundingBox(
        mPrevPlayerPosition, data.entityPosition - mPrevPlayerPosition);
    sweptPlayerBounds.inflate(data.entityRadius);

    // This is local trigger-query acceleration, not primitive generation culling
    // (ADR-0007): every primitive remains in every selected generation.
    for (auto triggerLine : findTriggerLines(sweptPlayerBounds)) {
      auto layer = triggerLine->getLayer();
      if (layer == BW_LAYER_ALL || data.layerSelection.test(size_t(layer))) {
        triggerLine->checkCollide(mPrevPlayerPosition, data.entityPosition, data.entityRadius);
      }
    }
  }

  for (auto primitive : mPrimitives) {
    primitive->updateTime(frameTime, data);
    primitive->setInputs(data.entityPosition, data.entityAngle, &mTriggerLines);

    if (mAlwaysUpdateVertices) {
      primitive->updateVertexPositions();
    }

    // Update animations and get any events
    globalEvents |= primitive->calculateAnimationValues();
  }

  // Handle the events after updating the WorldDataGenerator, so that the WorldDataGenerator
  // can pull the latest Primitives, in case an event fires off a clip request.
  mDataGenerator->update(frameTime, data, globalEvents);

  handleEvents(globalEvents);

  mPrevPlayerPosition = data.entityPosition;
}

void World::generateClipping(bool regetPrimitives) {
  mDataGenerator->generate(this, regetPrimitives);
}

vector<Primitive*> World::sortPrimitiveIndicesByPriority(vector<uint32_t> const& indices) const {
  vector<Primitive*> primitives;

  for (auto index : indices) {
    primitives.push_back(mPrimitives[index]);
  }

  stable_sort(
      primitives.begin(), primitives.end(),
      WorldDataGenerator::SortPrimitivesByPriority());

  return primitives;
}

void World::addPrimitiveToLookupGrid(Primitive* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  primitive->updateVertexPositions();
  primitive->calculateBounds();

  mPrimitiveLookupGrid->addItem(primitive->getId(), primitive->getBounds(), mPrimitiveCellMetadataUpdater);
}

void World::removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  mPrimitiveLookupGrid->removeItem(primitive->getId(), mPrimitiveCellMetadataUpdater, failIfNotFound);
}

void World::addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  mTriggerLookupGrid->addItem(triggerLine->getId(), triggerLine->getBounds());
}

void World::removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  mTriggerLookupGrid->removeItem(triggerLine->getId());
}

WorldDataPtr World::getWorldData() const {
  return mDataGenerator->getWorldData(this);
}

}  // namespace core
}  // namespace bw