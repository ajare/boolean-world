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
#include "core/Registry.h"
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
    : mExtents(-size / 2, -size / 2, size, size), mActiveLayerIndex(0), mNextLayerId(1), mPlayerStartPosition{0.0f, 0.0f}, mPlayerStartAngle(0.0f), mAlwaysUpdateVertices(false), mStepThreshold(numeric_limits<float>::infinity()), mFrameNumber(0), mDataGenerator(new DefaultWorldDataGenerator()), mPrevPlayerPosition{999999.0f, 999999.0f}, mLastPrimitiveUpdateFrameNumber(0) {
  mLayers.push_back(new Layer(0, "Layer 0", size, gridSize));
}

World::World(World const& other)
    : mActiveLayerIndex(0), mNextLayerId(1), mDataGenerator(nullptr) {
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
  swap(mLayers, other.mLayers);
  swap(mActiveLayerIndex, other.mActiveLayerIndex);
  swap(mNextLayerId, other.mNextLayerId);
  swap(mPlayerStartPosition, other.mPlayerStartPosition);
  swap(mPlayerStartAngle, other.mPlayerStartAngle);
  swap(mAlwaysUpdateVertices, other.mAlwaysUpdateVertices);
  swap(mStepThreshold, other.mStepThreshold);
  swap(mFrameNumber, other.mFrameNumber);
  swap(mDataGenerator, other.mDataGenerator);
  swap(mLastPrimitiveUpdateFrameNumber, other.mLastPrimitiveUpdateFrameNumber);
  swap(mPrevPlayerPosition, other.mPrevPlayerPosition);
}

void World::rebindOwnedState() {
  // Layer::copyFrom / Layer::rebindOwnedState rebind each Primitive's trigger
  // line inputs to its own Layer; only the back-link to the World is ours.
  for (auto* layer : mLayers) {
    layer->_setFrameNumber(mFrameNumber);

    for (auto* primitive : layer->getPrimitives()) {
      primitive->mWorld = this;
    }
  }

  if (mDataGenerator) {
    mDataGenerator->rebindToWorld(this);
  }
}

World::~World() {
  releaseOwnedState();
}

void World::releaseOwnedState() {
  delete mDataGenerator;
  mDataGenerator = nullptr;

  for (auto layer : mLayers) {
    delete layer;
  }

  mLayers.clear();
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
  mLastPrimitiveUpdateFrameNumber = other.mLastPrimitiveUpdateFrameNumber;

  // mActiveLayerIndex is intentionally left at 0 (set by the constructor's
  // member-init list) rather than copied from other: it is authoring
  // session state, not part of a World's value.
  mNextLayerId = other.mNextLayerId;
  for (auto layer : other.mLayers) {
    mLayers.push_back(new Layer(*layer));
  }

  // Layer's copy constructor deep-copies its own content but cannot know
  // which World now owns it.
  rebindOwnedState();

  // Data generator
  if (other.mDataGenerator) {
    mDataGenerator = other.mDataGenerator->copyForWorld(this);
  } else {
    mDataGenerator = nullptr;
  }
}

bool World::childrenModified() const {
  for (auto const* layer : mLayers) {
    if (layer->isModified()) {
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

      // Every Layer this World owns is written inline, each self-contained
      // (id, name, extents, its own Primitives/WorldTriggerLines), in order.
      // Neither the active-layer index nor the generation layer-selection
      // mask is part of this: both are transient authoring/runtime state
      // (docs/adr/0013).
      serializer->beginArray("layers");
      {
        for (auto const* layer : mLayers) {
          layer->serialize(serializer, workData);
        }

        serializer->endArray();  // layers
      }

      serializer->endMap();  // world
    }
  }

  serializer->endMap();  // root
}

bool World::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  // <= 0 conventionally means "keep whatever grid size the target World
  // already has" (see SerializationWorkData). Deserializing now constructs
  // fresh Layer objects rather than refilling the target's existing ones, so
  // that convention is honoured explicitly: fall back to the size the
  // pre-existing active Layer's grid was built with, read before it and its
  // siblings are replaced below.
  if (workData.accelGridSize <= 0.0f) {
    workData.accelGridSize = getPrimitiveAccelerationGridSize();
  }

  // Read in to temporary objects
  string worldName, description;
  wp::Vector2 minExtent, maxExtent;
  wp::Vector2 playerStartPosition;
  float playerStartAngle;
  float stepThreshold;
  vector<unique_ptr<Layer>> layers;

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

        // Each Layer parses and validates its own Primitives/
        // WorldTriggerLines - including its own parent-chain cycle check -
        // entirely on its own (Layer::deserializeImpl), self-contained
        // exactly as its serialized form is.
        serializer->beginArray("layers");
        {
          while (serializer->nextArrayItem()) {
            auto layer = make_unique<Layer>();
            auto const deserialized = layer->deserialize(serializer, workData);

            // A Layer that deserializes successfully may still have
            // recorded warnings (e.g. an unknown primitive parent id) that
            // this World's own callers expect to see.
            copyErrorsAndWarnings(layer.get(), !deserialized, true);

            if (!deserialized) {
              return false;
            }

            layers.push_back(move(layer));
          }

          serializer->endArray();  // layers
        }

        serializer->endMap();  // world
      }
    }

    serializer->endMap();  // root
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  if (layers.empty()) {
    addDeserializationError("World file contains no layers!");

    return false;
  }

  uint32_t numPrimitives{0};
  uint32_t numVertices{0};
  for (auto const& layer : layers) {
    numPrimitives += layer->getNumPrimitives();

    for (auto const* primitive : layer->getPrimitives()) {
      numVertices += primitive->getNumVertices();
    }
  }

  if (numPrimitives == 0 && !workData.allowEmptyWorld) {
    addDeserializationError("World file contains no primitives!");

    return false;
  }

  if (numVertices > BW_VERTEX_COUNT_USEABLE_MAX) {
    addDeserializationError("The World contains too many vertices");

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

  for (auto layer : mLayers) {
    delete layer;
  }
  mLayers.clear();

  uint32_t nextLayerId{0};
  for (auto& layer : layers) {
    nextLayerId = max(nextLayerId, layer->getId() + 1);
    mLayers.push_back(layer.release());
  }
  mNextLayerId = nextLayerId;

  // The active Layer index is never part of the serialized format: a
  // deserialized World always starts focused on its first Layer.
  mActiveLayerIndex = 0;

  mFrameNumber = 0;
  mLastPrimitiveUpdateFrameNumber = 0;

  auto* activeLayer = getActiveLayer();

  // The generation layer selection is never serialized: a loaded World always
  // starts scoped to just its active Layer, never a mask carried over from
  // the previous contents (docs/adr/0013).
  if (mDataGenerator) {
    mDataGenerator->_resetLayerSelection(SelectLayer(activeLayer->getId()));
  }

  // Calculate vertices/bounds to initialise every Layer's Primitives, not
  // just the active one: a generation may fold across any selected set.
  for (auto* layer : mLayers) {
    layer->_setFrameNumber(0);

    for (auto primitive : layer->getPrimitives()) {
      primitive->mWorld = this;

      primitive->updateTime(0.0, {wp::Vector2::ZERO,
                                  BW_PLAYER_RADIUS,
                                  BW_PLAYER_FOV,
                                  BW_PLAYER_VIEW_DISTANCE,
                                  false,
                                  false,
                                  0});

      primitive->setInputs(wp::Vector2::ZERO, 0.0f, layer->_getTriggerLineStorage());
      primitive->calculateAnimationValues();
      primitive->updateVertexPositions();

      // Layer::addPrimitive (used internally by Layer::deserializeImpl) does
      // not compute a deserialized Primitive's bounds - only World::
      // addPrimitive does, for normal authoring adds. Do that now that
      // mWorld is bound, so the acceleration grid ends up holding this
      // Primitive at its real bounds rather than its default-constructed
      // ones.
      primitive->invalidatePostTransform(true, true);
    }
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
  for (auto layer : mLayers) {
    layer->createAccelerationGrids(targetCellSize);
  }
}

float World::getPrimitiveAccelerationGridSize() const {
  return getActiveLayer()->getPrimitiveAccelerationGridSize();
}

void World::clear() {
  mName = "";
  mDescription = "";
  mFrameNumber = 0;
  mLastPrimitiveUpdateFrameNumber = 0;

  releaseOwnedState();

  // A World always owns at least one Layer, so a cleared World stays usable:
  // re-seed the default Layer, gridless, as the constructor would.
  mLayers.push_back(new Layer(0, "Layer 0", mExtents.getSize().x, -1.0f));
  mActiveLayerIndex = 0;
  mNextLayerId = 1;
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

vector<Layer*> const& World::getLayers() const {
  return mLayers;
}

uint32_t World::getNumLayers() const {
  return (uint32_t)mLayers.size();
}

uint32_t World::getActiveLayerIndex() const {
  return mActiveLayerIndex;
}

Layer* World::getActiveLayer() {
  return mLayers[mActiveLayerIndex];
}

Layer const* World::getActiveLayer() const {
  return mLayers[mActiveLayerIndex];
}

void World::setActiveLayer(Layer* layer) {
  auto numLayers = (uint32_t)mLayers.size();
  for (uint32_t i = 0; i < numLayers; ++i) {
    if (mLayers[i] == layer) {
      mActiveLayerIndex = i;
      return;
    }
  }

  throw CoreException("Layer not found in World");
}

Layer* World::getLayer(uint32_t id) {
  for (auto* layer : mLayers) {
    if (layer->getId() == id) {
      return layer;
    }
  }

  return nullptr;
}

Layer const* World::getLayer(uint32_t id) const {
  for (auto const* layer : mLayers) {
    if (layer->getId() == id) {
      return layer;
    }
  }

  return nullptr;
}

Layer* World::addLayer(string const& name) {
  auto id = mNextLayerId++;

  auto layer = new Layer(id, name, mExtents.getSize().x, getPrimitiveAccelerationGridSize());

  mLayers.push_back(layer);

  return layer;
}

Layer* World::addLayer(Layer* layer) {
  auto const colliding = any_of(mLayers.begin(), mLayers.end(), [&](Layer const* existing) {
    return existing->getId() == layer->getId();
  });

  if (colliding) {
    layer->_setId(mNextLayerId);
  }

  mNextLayerId = max(mNextLayerId, layer->getId() + 1);

  mLayers.push_back(layer);

  return layer;
}

void World::removeLayer(Layer* layer, bool failIfNotFound) {
  if (mLayers.size() <= 1) {
    throw CoreException("Cannot remove the last remaining Layer from a World");
  }

  auto numLayers = (uint32_t)mLayers.size();
  for (uint32_t i = 0; i < numLayers; ++i) {
    if (mLayers[i] == layer) {
      delete layer;
      mLayers.erase(mLayers.begin() + i);

      // Keep the active Layer's identity stable across the removal: if it
      // was the one removed, fall back to the first Layer; otherwise track
      // its new position.
      if (mActiveLayerIndex == i) {
        mActiveLayerIndex = 0;
      } else if (mActiveLayerIndex > i) {
        mActiveLayerIndex--;
      }

      return;
    }
  }

  if (failIfNotFound) {
    throw CoreException("Layer not found in World");
  }
}

void World::removeLayer(uint32_t index) {
  assert(index < getNumLayers() && "World::removeLayer(index) - index out of bounds");

  removeLayer(mLayers[index]);
}

void World::moveLayer(uint32_t fromIndex, uint32_t toIndex) {
  assert(fromIndex < getNumLayers() && "World::moveLayer(fromIndex, toIndex) - fromIndex out of bounds");
  assert(toIndex < getNumLayers() && "World::moveLayer(fromIndex, toIndex) - toIndex out of bounds");

  if (fromIndex == toIndex) {
    return;
  }

  auto activeLayer = mLayers[mActiveLayerIndex];

  auto layer = mLayers[fromIndex];
  mLayers.erase(mLayers.begin() + fromIndex);
  mLayers.insert(mLayers.begin() + toIndex, layer);

  // Reordering must not silently change which Layer is being authored.
  auto it = find(mLayers.begin(), mLayers.end(), activeLayer);
  mActiveLayerIndex = (uint32_t)distance(mLayers.begin(), it);
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

frame_number_type World::getFrameNumber() const {
  return mFrameNumber;
}

bool World::getGridSettings(int* dimX, int* dimY, float* cellSize) {
  return getActiveLayer()->getGridSettings(dimX, dimY, cellSize);
}

void World::_cachePrimitiveStaticness(bool cache) {
  for (auto prim : getActiveLayer()->getPrimitives()) {
    prim->cacheStaticness(cache);
  }
}

uint32_t World::addTriggerLine(WorldTriggerLine* triggerLine) {
  return getActiveLayer()->addTriggerLine(triggerLine);
}

vector<WorldTriggerLine*> World::findTriggerLines(wp::BoundingBox const& bounds) const {
  return getActiveLayer()->findTriggerLines(bounds);
}

uint32_t World::addPrimitive(Primitive* primitive) {
  auto index = getActiveLayer()->addPrimitive(primitive);

  primitive->mWorld = this;

  primitive->invalidatePostTransform(true, true);

  return index;
}

void World::removePrimitive(Primitive* primitive, bool failIfNotFound) {
  getActiveLayer()->removePrimitive(primitive, failIfNotFound);
}

void World::removePrimitive(uint32_t index) {
  assert(index < getNumPrimitives() && "World::removePrimitive(index) - index out of bounds");

  getActiveLayer()->removePrimitive(index);
}

void World::removePrimitives(vector<uint32_t> const& indices) {
  getActiveLayer()->removePrimitives(indices);
}

void World::replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound) {
  assert(index < getNumPrimitives() && "World::replacePrimitive(index, newPrimitive) - index out of bounds");

  getActiveLayer()->replacePrimitive(index, newPrimitive, failIfNotFound);

  newPrimitive->mWorld = this;
}

void World::removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound) {
  getActiveLayer()->removeTriggerLine(triggerLine, failIfNotFound);
}

void World::removeTriggerLine(uint32_t index) {
  assert(index < getNumTriggerLines() && "World::removeTriggerLine(index) - index out of bounds");

  getActiveLayer()->removeTriggerLine(index);
}

void World::removeTriggerLines(vector<uint32_t> const& indices) {
  getActiveLayer()->removeTriggerLines(indices);
}

void World::replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound) {
  assert(index < getNumTriggerLines() && "World::replaceTriggerLine(index, newTriggerLine) - index out of bounds");

  getActiveLayer()->replaceTriggerLine(index, newTriggerLine, failIfNotFound);
}

void World::setTriggerLinePoint(uint32_t triggerLineIndex, uint32_t pointIndex, wp::Vector2 const& position) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::setTriggerLinePoint - trigger line index out of bounds");
  assert(pointIndex < 2 && "World::setTriggerLinePoint - point index out of bounds");

  auto const* triggerLine = getTriggerLine(triggerLineIndex);
  auto p0 = triggerLine->getPoint(0);
  auto p1 = triggerLine->getPoint(1);
  (pointIndex == 0 ? p0 : p1) = position;
  setTriggerLinePoints(triggerLineIndex, p0, p1);
}

void World::setTriggerLinePoints(uint32_t triggerLineIndex, wp::Vector2 const& p0, wp::Vector2 const& p1) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::setTriggerLinePoints - trigger line index out of bounds");

  getActiveLayer()->setTriggerLinePoints(triggerLineIndex, p0, p1);
}

void World::moveTriggerLine(uint32_t triggerLineIndex, wp::Vector2 const& offset) {
  assert(triggerLineIndex < getNumTriggerLines() && "World::moveTriggerLine - trigger line index out of bounds");

  auto const* triggerLine = getTriggerLine(triggerLineIndex);
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
  mLastPrimitiveUpdateFrameNumber = mFrameNumber;

  // A mutated Primitive is not necessarily on the active Layer - #163 lets
  // content live on any Layer - so its own acceleration grid, not
  // necessarily the active Layer's, is the one that needs updating.
  for (auto* layer : mLayers) {
    auto const& primitives = layer->getPrimitives();
    if (find(primitives.begin(), primitives.end(), primitive) != primitives.end()) {
      layer->primitiveChanged(primitive);
      return;
    }
  }

  throw CoreException("Primitive not found in any Layer of this World.");
}

Primitive* World::getPrimitive(uint32_t index) {
  assert(index < getNumPrimitives() && "World::getPrimitive(index) - index out of bounds");

  return getActiveLayer()->getPrimitive(index);
}

Primitive const* World::getPrimitive(uint32_t index) const {
  assert(index < getNumPrimitives() && "World::getPrimitive(index) - index out of bounds");

  return getActiveLayer()->getPrimitive(index);
}

uint32_t World::getNumPrimitives() const {
  return getActiveLayer()->getNumPrimitives();
}

vector<Primitive*> const& World::getPrimitives() const {
  return getActiveLayer()->getPrimitives();
}

vector<Primitive*> World::getPrimitivesByPriority() const {
  auto sorted = getPrimitives();

  stable_sort(
      sorted.begin(), sorted.end(),
      WorldDataGenerator::SortPrimitivesByPriority());

  return sorted;
}

uint32_t World::getNumTriggerLines() const {
  return getActiveLayer()->getNumTriggerLines();
}

vector<WorldTriggerLine*> const& World::getTriggerLines() const {
  return getActiveLayer()->getTriggerLines();
}

WorldTriggerLine* World::getTriggerLine(uint32_t index) const {
  return getActiveLayer()->getTriggerLine(index);
}

uint32_t World::findTriggerLineIndex(wp::Vector2 const& worldPos, float tolerance, float handleRadius) const {
  auto const& triggerLines = getTriggerLines();
  auto numTriggerLines = (uint32_t)triggerLines.size();
  auto handleRadiusSq = handleRadius * handleRadius;
  auto toleranceSq = tolerance * tolerance;

  for (uint32_t i = 0; i < numTriggerLines; ++i) {
    auto triggerLine = triggerLines[i];
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
  getActiveLayer()->getGridCellFrameNumber(cellIndex, frameNumber);
}

vector<Primitive*> World::getPrimitivesInGridCell(uint32_t cellIndex) const {
  vector<Primitive*> result;

  auto const* layer = getActiveLayer();

  for (auto primIndex : layer->getPrimitiveIndicesInGridCell(cellIndex)) {
    result.push_back(layer->getPrimitive(primIndex));
  }

  return result;
}

Primitive* World::findPrimitive(wp::Vector2 const& worldPos) const {
  auto index = findPrimitiveIndex(worldPos, false);

  return index != ~0u ? getActiveLayer()->getPrimitive(index) : nullptr;
}

uint32_t World::findPrimitiveIndex(wp::Vector2 const& worldPos, bool exact, set<uint32_t> const& ignoreIndices) const {
  auto const* layer = getActiveLayer();
  auto const candidates = layer->getPrimitiveCandidateIndices(worldPos);

  for (auto i : candidates) {
    auto primitive = layer->getPrimitive(i);

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
  auto const* layer = getActiveLayer();
  auto const candidates = layer->getPrimitiveCandidateIndices(worldPos);

  for (auto i : candidates) {
    auto primitive = layer->getPrimitive(i);

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
  return getActiveLayer()->findPrimitives(bounds);
}

vector<Primitive*> World::findPrimitives(wp::BoundingCircle const& bounds) const {
  return getActiveLayer()->findPrimitives(bounds);
}

void World::handleEvents(uint32_t /*events*/) {
}

void World::update(float frameTime, WorldUpdateData const& data, wp::Vector2 const& viewSize) {
  mFrameNumber++;

  auto* activeLayer = getActiveLayer();

  // Grid cell metadata is stamped with the World's clock, which only the
  // World advances.
  for (auto* layer : mLayers) {
    layer->_setFrameNumber(mFrameNumber);
  }

  uint32_t globalEvents{0};

  if (mPrevPlayerPosition.x < 999998.0f) {
    auto sweptPlayerBounds = wp::BoundingBox(
        mPrevPlayerPosition, data.entityPosition - mPrevPlayerPosition);
    sweptPlayerBounds.inflate(data.entityRadius);

    // This is local trigger-query acceleration, not primitive generation culling
    // (ADR-0007): every primitive remains in every selected generation.
    //
    // A WorldTriggerLine belongs to the Layer that owns it, so the same
    // Layer-id selection that scopes generation scopes trigger collision -
    // each selected Layer answers from its own trigger grid.
    for (auto* layer : mLayers) {
      if (!IsLayerSelected(data.layerSelection, layer->getId())) {
        continue;
      }

      for (auto triggerLine : layer->findTriggerLines(sweptPlayerBounds)) {
        triggerLine->checkCollide(mPrevPlayerPosition, data.entityPosition, data.entityRadius);
      }
    }
  }

  for (auto primitive : activeLayer->getPrimitives()) {
    primitive->updateTime(frameTime, data);
    primitive->setInputs(data.entityPosition, data.entityAngle, activeLayer->_getTriggerLineStorage());

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

  auto const* layer = getActiveLayer();
  for (auto index : indices) {
    primitives.push_back(layer->getPrimitive(index));
  }

  stable_sort(
      primitives.begin(), primitives.end(),
      WorldDataGenerator::SortPrimitivesByPriority());

  return primitives;
}

WorldDataPtr World::getWorldData() const {
  return mDataGenerator->getWorldData(this);
}

}  // namespace core
}  // namespace bw