#include <iterator>
#include <limits>
#include <map>
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

World::World(float size, float gridSize, WorldDataGeneratorFactory generatorFactory)
    : mExtents(-size / 2, -size / 2, size, size), mPlayerStartPosition{0.0f, 0.0f}, mPlayerStartAngle(0.0f), mAlwaysUpdateVertices(false), mStepThreshold(numeric_limits<float>::infinity()), mFrameNumber(0), mDataGenerator(nullptr), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr), mPrevPlayerPosition{999999.0f, 999999.0f}, mPrefabAreaTilingType(PrefabAreaTilingType::None), mPrefabAreaTileTypes(0), mLastPrimitiveUpdateFrameNumber(0) {
  mPrimitiveCellMetadataUpdater = bind(&World::updatePrimitiveCellMetadata, this, placeholders::_1);

  if (gridSize > 0.0f) {
    createAccelerationGrids(gridSize);

    if (generatorFactory) {
      mDataGenerator = generatorFactory(
          getExtents().getMinExtent(),
          mPrimitiveLookupGrid->getCellDimensionX(),
          mPrimitiveLookupGrid->getCellDimensionY(),
          mPrimitiveLookupGrid->getCellSize().x);
    } else {
      mDataGenerator = new DefaultWorldDataGenerator();
    }
  } else {
    if (generatorFactory) {
      mDataGenerator = generatorFactory(getExtents().getMinExtent(), 1, 1, size);
    } else {
      mDataGenerator = new DefaultWorldDataGenerator();
    }
  }
}

World::World(World const& other) {
  mPrimitiveCellMetadataUpdater = bind(&World::updatePrimitiveCellMetadata, this, placeholders::_1);

  copyFrom(other);
}

World& World::operator=(World const& other) {
  mPrimitiveCellMetadataUpdater = bind(&World::updatePrimitiveCellMetadata, this, placeholders::_1);

  copyFrom(other);
  return *this;
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

  // Create copies of primitives, making sure to instantiate correct subclass
  map<Primitive*, Primitive*> primitiveMap;
  for (auto primitive : other.mPrimitives) {
    auto p = primitive->copy();

    primitiveMap[primitive] = p;

    addPrimitive(p);
  }

  for (auto triggerLine : other.mTriggerLines) {
    auto tl = new WorldTriggerLine(*triggerLine);

    addTriggerLine(tl);
  }

  // Data generator
  if (other.mDataGenerator) {
    mDataGenerator = other.mDataGenerator->copy();
  } else {
    mDataGenerator = nullptr;
  }
}

Primitive* World::instantiatePrimitive(string const& type) const {
  typedef function<Primitive*()> PrimitiveCreator;

  map<string, PrimitiveCreator> primCreators = {
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
          if ((primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0) {
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
  // Read in to temporary objects
  string worldName, description;
  wp::Vector2 minExtent, maxExtent;
  wp::Vector2 playerStartPosition;
  float playerStartAngle;
  float stepThreshold;
  PrefabAreaTilingType prefabAreaTilingType;
  uint32_t prefabAreaTileTypes;
  vector<Primitive*> primitives;
  vector<WorldTriggerLine*> triggerLines;

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
              auto primitive = instantiatePrimitive(primitiveType);

              if (!primitive->deserialize(serializer, workData)) {
                copyErrorsAndWarnings(primitive, true, true);
                return false;
              }

              numVertices += primitive->getNumVertices();

              if (numVertices > BW_VERTEX_COUNT_USEABLE_MAX) {
                throw CoreException("The World contains too many vertices");
              }

              primitives.push_back(primitive);

              serializer->endMap();  // primitive
            }
          }

          serializer->endArray();  // primitives
        }

        serializer->beginArray("triggerLines");
        {
          while (serializer->nextArrayItem()) {
            // Instantiate triggerline and deserialize
            auto triggerLine = new WorldTriggerLine();

            if (!triggerLine->deserialize(serializer, workData)) {
              copyErrorsAndWarnings(triggerLine, true, true);
              return false;
            }

            triggerLines.push_back(triggerLine);
          }

          serializer->endArray();  // triggerlines
        }
      }

      // Fix up VertexTransformer parents
      for (auto& item : workData.vtoIdToVtoMap) {
        auto& [id, vt] = item;

        auto it = workData.vtoIdToParentMap.find(id);
        if (it != workData.vtoIdToParentMap.end()) {
          auto parentId = it->second;
          vt->setParent(workData.vtoIdToVtoMap[parentId]);
        }
      }

      serializer->endMap();  // world
    }

    serializer->endMap();  // root
  } catch (exception& e) {
    addDeserializationError(e.what());

    // No memory leaks on failed open
    for (auto primitive : primitives) {
      delete primitive;
    }

    for (auto triggerLine : triggerLines) {
      delete triggerLine;
    }

    return false;
  }

  if (primitives.empty()) {
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
  mAlwaysUpdateVertices = false;
  mStepThreshold = stepThreshold;
  mPrefabAreaTilingType = prefabAreaTilingType;
  mPrefabAreaTileTypes = prefabAreaTileTypes;

  // Create accel grids after setting extents but before adding primitives
  if (workData.accelGridSize > 0.0f) {
    delete mPrimitiveLookupGrid;
    delete mTriggerLookupGrid;

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
  for (auto triggerLine : triggerLines) {
    addTriggerLine(triggerLine);
  }

  // Fix up Primitive indices.  Because we might be dealing with
  // ghost Primitives, a World might have been saved which does not
  // have sequential indices.  We need to make sure this is enforecd, though.
  // Invalidate each primitive so it recalculates its transformed vertices.
  for (auto primitive : primitives) {
    addPrimitive(primitive);
    primitive->_invalidate();
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

void World::setWorldDataGeneratorFactory(WorldDataGeneratorFactory generatorFactory) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("Cannot use a WorldDataGeneratorFactory without acceleration grids.");
  }

  delete mDataGenerator;
  mDataGenerator = generatorFactory(
      getExtents().getMinExtent(),
      mPrimitiveLookupGrid->getCellDimensionX(),
      mPrimitiveLookupGrid->getCellDimensionY(),
      mPrimitiveLookupGrid->getCellSize().x);
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

  set<uint32_t> indices = mTriggerLookupGrid->getCandidateItemsInBoundingArea(bounds);

  for (auto index : indices) {
    auto triggerLine = mTriggerLines[index];

    result.push_back(triggerLine);
  }

  return result;
}

uint32_t World::addPrimitive(Primitive* primitive) {
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
  if (mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid->removeItem(primitive->getId(), mPrimitiveCellMetadataUpdater);
  }

  auto numPrimitives = (uint32_t)mPrimitives.size();
  for (uint32_t i = 0; i < numPrimitives; ++i) {
    if (mPrimitives[i] == primitive) {
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
  uint32_t tCount{0};
  auto numPrimitives = (uint32_t)mPrimitives.size();
  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto found = find(indices.begin(), indices.end(), i) != indices.end();
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
  } catch (exception& e) {
    if (failIfNotFound) {
      throw e;
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
  } catch (exception& e) {
    if (failIfNotFound) {
      throw e;
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

Primitive* World::createMeshPrimitive(vector<Primitive*> const& fold) const {
  auto selected = fold;
  stable_sort(
      selected.begin(), selected.end(),
      [](Primitive const* lhs, Primitive const* rhs) {
        return lhs->getPriority() < rhs->getPriority();
      });
  ArrangementWorldDataGenerator generator;
  generator.generate(selected);
  auto arrangement = generator.getWorldData();

  auto boundaryVertices = [&](vector<uint32_t> const& boundary) {
    ClosedPolygon polygon;
    if (boundary.empty()) {
      return polygon;
    }
    auto firstEdge = arrangement->edges[boundary.front()];
    uint32_t current = firstEdge.v[0];
    uint32_t next = firstEdge.v[1];
    if (boundary.size() > 1) {
      auto secondEdge = arrangement->edges[boundary[1]];
      auto firstEndpointContinues =
          secondEdge.v[0] == firstEdge.v[0] ||
          secondEdge.v[1] == firstEdge.v[0];
      if (firstEndpointContinues) {
        current = firstEdge.v[1];
        next = firstEdge.v[0];
      }
    }
    polygon.push_back(
        {{arr::ToWorldCoordinate(arrangement->vertices[current].x),
          arr::ToWorldCoordinate(arrangement->vertices[current].y)}});
    for (size_t i = 1; i < boundary.size(); ++i) {
      polygon.push_back(
          {{arr::ToWorldCoordinate(arrangement->vertices[next].x),
            arr::ToWorldCoordinate(arrangement->vertices[next].y)}});
      auto edge = arrangement->edges[boundary[i]];
      next = edge.v[0] == next ? edge.v[1] : edge.v[0];
    }
    return polygon;
  };

  vector<ComplexPolygon> polygons;
  for (auto const& face : arrangement->faces) {
    if (!face.solid || face.outerBoundary.empty()) {
      continue;
    }
    ComplexPolygon polygon;
    polygon.push_back(boundaryVertices(face.outerBoundary));
    for (auto const& hole : face.innerBoundaries) {
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
  mLastPrimitiveUpdateFrameNumber = mFrameNumber;

  auto id = primitive->getId();

  // Get the containing grid cell(s), and set the version
  auto cellIndices = mPrimitiveLookupGrid->_getItemCellIndices(id);

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
  vector<Primitive*> sorted(mPrimitives.size());

  partial_sort_copy(mPrimitives.begin(), mPrimitives.end(), sorted.begin(), sorted.end(), SortPrimitivesByPriority());

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

void World::getGridCellPrimitivesVersion(uint32_t cellIndex, frame_number_type* primitivesVersion) const {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  *primitivesVersion = mPrimitiveLookupGrid->getUser(cellIndex).lastUpdatedFrameNumber;
}

void World::getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  *frameNumber = mPrimitiveLookupGrid->getUser(cellIndex).lastUpdatedFrameNumber;
}

vector<Primitive*> World::getPrimitivesInGridCell(uint32_t cellIndex, uint8_t activeLayer, frame_number_type* primitivesVersion) const {
  vector<Primitive*> result;

  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  auto primIndices = mPrimitiveLookupGrid->_getCellItemIndices(cellIndex);
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

uint32_t World::findPrimitiveIndex(wp::Vector2 const& worldPos, bool exact, set<uint32_t> ignoreIndices) const {
  auto numPrimitives = getNumPrimitives();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = mPrimitives[i];

    auto const& bounds = primitive->getBounds();
    if (bounds.pointInside(worldPos) && ignoreIndices.find(i) == ignoreIndices.end()) {
      if (exact) {
        // Create triangulation and check that
        auto triangulation = primitive->triangulate(true, nullptr);
        if (triangulation.pointInside(worldPos)) {
          return i;
        }
      } else {
        return i;
      }
    }
  }

  return ~0u;
}

vector<uint32_t> World::findPrimitiveIndices(wp::Vector2 const& worldPos, bool exact, set<uint32_t> ignoreIndices) const {
  vector<uint32_t> result;
  auto numPrimitives = getNumPrimitives();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = mPrimitives[i];

    if (!primitive->hasFlag(BW_PRIMITIVE_INTERACTS_FLAG)) {
      continue;
    }

    auto const& bounds = primitive->getBounds();
    if (bounds.pointInside(worldPos) && ignoreIndices.find(i) == ignoreIndices.end()) {
      if (exact) {
        // Create triangulation and check that
        auto triangulation = primitive->triangulate(true, nullptr);
        if (triangulation.pointInside(worldPos)) {
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

  set<uint32_t> indices = mPrimitiveLookupGrid->getCandidateItemsInBoundingArea(bounds);

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

  set<uint32_t> indices = mPrimitiveLookupGrid->getCandidateItemsInBoundingArea(bounds);

  for (auto index : indices) {
    auto primitive = mPrimitives[index];

    if (primitive->hasFlag(BW_PRIMITIVE_INTERACTS_FLAG)) {
      result.push_back(primitive);
    }
  }

  return result;
}

void World::handleEvents(uint32_t events) {
  if (events & BW_PRIMITIVE_GLOBAL_EVENT_DEBUG) {
    int x = 5;
  }
}

void World::update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize) {
  mFrameNumber++;

  uint32_t globalEvents{0};

  if (mPrevPlayerPosition.x < 999998.0f) {
    for (auto triggerLine : mTriggerLines) {
      if (triggerLine->getLayer() == data.activeLayer) {
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

  sort(primitives.begin(), primitives.end(), SortPrimitivesByPriority());

  return primitives;
}

void World::addPrimitiveToLookupGrid(Primitive* primitive) {
  primitive->updateVertexPositions();
  primitive->calculateBounds();

  mPrimitiveLookupGrid->addItem(primitive->getId(), primitive->getBounds(), mPrimitiveCellMetadataUpdater);
}

void World::removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound) {
  mPrimitiveLookupGrid->removeItem(primitive->getId(), mPrimitiveCellMetadataUpdater, failIfNotFound);
}

void World::addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine) {
  mTriggerLookupGrid->addItem(triggerLine->getId(), triggerLine->getBounds());
}

void World::removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine) {
  mTriggerLookupGrid->removeItem(triggerLine->getId());
}

WorldDataPtr World::getWorldData(wp::Vector2 const& position, float angle) const {
  return mDataGenerator->getWorldData(this);
}

}  // namespace core
}  // namespace bw