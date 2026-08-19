#include <map>
#include <memory>
#include <algorithm>
#include <cassert>

#include "core/Layer.h"
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

namespace bw {
namespace core {

using namespace std;

Layer::Layer()
    : Layer(0, "", BW_WORLD_SIZE, -1.0f) {
}

Layer::Layer(uint32_t id, string const& name, float size, float gridSize)
    : mId(id), mName(name), mExtents(-size / 2, -size / 2, size, size), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr) {
  mPrimitiveCellMetadataUpdater = bind(&Layer::updatePrimitiveCellMetadata, this, placeholders::_1);

  if (gridSize > 0.0f) {
    createAccelerationGrids(gridSize);
  }
}

Layer::Layer(Layer const& other)
    : mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr) {
  mPrimitiveCellMetadataUpdater = bind(&Layer::updatePrimitiveCellMetadata, this, placeholders::_1);

  copyFrom(other);
}

Layer& Layer::operator=(Layer const& other) {
  if (this == &other) {
    return *this;
  }

  Layer replacement(other);
  swapState(replacement);
  rebindOwnedState();
  replacement.rebindOwnedState();
  return *this;
}

void Layer::swapState(Layer& other) noexcept {
  Serializable::swapState(other);

  using std::swap;
  swap(mId, other.mId);
  swap(mName, other.mName);
  swap(mExtents, other.mExtents);
  swap(mPrimitives, other.mPrimitives);
  swap(mTriggerLines, other.mTriggerLines);
  swap(mPrimitiveLookupGrid, other.mPrimitiveLookupGrid);
  swap(mTriggerLookupGrid, other.mTriggerLookupGrid);
}

void Layer::rebindOwnedState() {
  for (auto* primitive : mPrimitives) {
    primitive->mWorld = nullptr;
    primitive->mInputs.triggerLines = &mTriggerLines;
  }
}

Layer::~Layer() {
  clear();
}

void Layer::copyFrom(Layer const& other) {
  Serializable::copyFrom(other);

  mId = other.mId;
  mName = other.mName;
  mExtents = other.mExtents;

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
  // refer to this Layer's collection immediately.
  for (auto triggerLine : other.mTriggerLines) {
    auto tl = make_unique<WorldTriggerLine>(*triggerLine);
    addTriggerLine(tl.get());
    tl.release();
  }

  // Clone every primitive before adding any of them, so parent links can be
  // remapped as a complete graph regardless of primitive ordering.
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
}

Primitive* Layer::instantiatePrimitive(string const& type) const {
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

bool Layer::childrenModified() const {
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

void Layer::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("layer");
  {
    serializer->writeUint32("id", mId);
    serializer->writeString("name", mName);
    serializer->writeVector2("minExtent", mExtents.getMinExtent());
    serializer->writeVector2("maxExtent", mExtents.getMaxExtent());

    serializer->beginArray("primitives");
    {
      for (auto const* primitive : mPrimitives) {
        if (!workData.includeGhostPrimitives &&
            (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0) {
          continue;
        }

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

      serializer->endArray();  // triggerLines
    }

    serializer->endMap();  // layer
  }
}

bool Layer::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  workData.vtoIdToVtoMap.clear();
  workData.vtoIdToParentMap.clear();

  uint32_t id;
  string name;
  wp::Vector2 minExtent, maxExtent;
  vector<unique_ptr<Primitive>> primitives;
  vector<unique_ptr<WorldTriggerLine>> triggerLines;

  try {
    serializer->beginMap("layer");
    {
      id = serializer->readUint32("id");
      name = serializer->readString("name", true);
      minExtent = serializer->readVector2("minExtent");
      maxExtent = serializer->readVector2("maxExtent");

      serializer->beginArray("primitives");
      {
        while (serializer->nextArrayItem()) {
          serializer->beginMap("primitive");
          {
            auto primitiveType = serializer->readString("type");

            auto primitive = unique_ptr<Primitive>(instantiatePrimitive(primitiveType));

            if (!primitive->deserialize(serializer, workData)) {
              copyErrorsAndWarnings(primitive.get(), true, true);
              return false;
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
          auto triggerLine = make_unique<WorldTriggerLine>();

          if (!triggerLine->deserialize(serializer, workData)) {
            copyErrorsAndWarnings(triggerLine.get(), true, true);
            return false;
          }

          triggerLines.push_back(move(triggerLine));
        }

        serializer->endArray();  // triggerLines
      }

      serializer->endMap();  // layer
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Fix up VertexTransformer parents.
  for (auto const& [vtoId, vt] : workData.vtoIdToVtoMap) {
    auto parentIdIt = workData.vtoIdToParentMap.find(vtoId);
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

  // Commit
  mId = id;
  mName = name;
  mExtents.setPosition(minExtent);
  mExtents.setSize(maxExtent - minExtent);

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

  // Add TriggerLines before Primitives, as Primitives may need them for
  // their initial values.
  for (auto& triggerLine : triggerLines) {
    addTriggerLine(triggerLine.get());
    triggerLine.release();
  }

  for (auto& primitive : primitives) {
    auto* addedPrimitive = primitive.get();
    addPrimitive(addedPrimitive);
    primitive.release();
    addedPrimitive->_invalidate();
  }

  return true;
}

void Layer::createAccelerationGrids(float targetCellSize) {
  if ((mExtents.getSize().x / targetCellSize) > BW_PRIMITIVE_GRID_DIM_MAX) {
    throw CoreException(format("Grid sizes cannot be greater than {}.", BW_PRIMITIVE_GRID_DIM_MAX));
  }

  wp::Vector2 minExtent, maxExtent;
  mExtents.getExtents(minExtent, maxExtent);

  auto layerOffset = minExtent;
  auto layerSize = maxExtent - minExtent;

  int dimsX = max(1, (int)(layerSize.x / targetCellSize));
  int dimsY = max(1, (int)(layerSize.y / targetCellSize));

  if (!mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid = new PrimitiveAccelerationGrid(layerOffset, layerSize, dimsX, dimsY, 0.0f);
  }

  if (!mTriggerLookupGrid) {
    mTriggerLookupGrid = new wp::AccelerationGrid(layerOffset, layerSize, dimsX, dimsY, 0.0f);
  }
}

void Layer::clear() {
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

uint32_t Layer::getId() const {
  return mId;
}

void Layer::setName(string const& name) {
  mName = name;
}

string const& Layer::getName() const {
  return mName;
}

wp::BoundingBox const& Layer::getExtents() const {
  return mExtents;
}

void Layer::updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata) {
  // Layer has no frame-number concept of its own yet (that lives on World);
  // cell metadata is otherwise unused until generation is wired up to it.
  BW_UNUSED(metadata);
}

uint32_t Layer::addPrimitive(Primitive* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  if (getNumPrimitives() >= BW_WORLD_PRIMITIVE_COUNT_MAX) {
    throw CoreException("Too many primitives added to the Layer");
  }

  auto index = (uint32_t)mPrimitives.size();

  mPrimitives.push_back(primitive);

  primitive->setId(index);
  primitive->setInputs(wp::Vector2::ZERO, 0.0f, &mTriggerLines);

  addPrimitiveToLookupGrid(primitive);

  primitive->_invalidate();

  return index;
}

void Layer::removePrimitive(Primitive* primitive, bool failIfNotFound) {
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
    throw CoreException(format("{} primitive {} not found in layer", primitive->getType(), primitive->getName()));
  }
}

uint32_t Layer::getNumPrimitives() const {
  return (uint32_t)mPrimitives.size();
}

vector<Primitive*> const& Layer::getPrimitives() const {
  return mPrimitives;
}

Primitive* Layer::getPrimitive(uint32_t index) const {
  assert(index < getNumPrimitives() && "Layer::getPrimitive(index) - index out of bounds");

  return mPrimitives[index];
}

vector<Primitive*> Layer::findPrimitives(wp::BoundingBox const& bounds) const {
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

vector<Primitive*> Layer::findPrimitives(wp::BoundingCircle const& bounds) const {
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

void Layer::addPrimitiveToLookupGrid(Primitive* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  primitive->updateVertexPositions();
  primitive->calculateBounds();

  mPrimitiveLookupGrid->addItem(primitive->getId(), primitive->getBounds(), mPrimitiveCellMetadataUpdater);
}

void Layer::removePrimitiveFromLookupGrid(Primitive const* primitive, bool failIfNotFound) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  mPrimitiveLookupGrid->removeItem(primitive->getId(), mPrimitiveCellMetadataUpdater, failIfNotFound);
}

uint32_t Layer::addTriggerLine(WorldTriggerLine* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  auto index = (uint32_t)mTriggerLines.size();

  mTriggerLines.push_back(triggerLine);

  triggerLine->setId(index);
  addTriggerLineToLookupGrid(triggerLine);

  return index;
}

void Layer::removeTriggerLine(WorldTriggerLine* triggerLine, bool failIfNotFound) {
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

uint32_t Layer::getNumTriggerLines() const {
  return (uint32_t)mTriggerLines.size();
}

vector<WorldTriggerLine*> const& Layer::getTriggerLines() const {
  return mTriggerLines;
}

WorldTriggerLine* Layer::getTriggerLine(uint32_t index) const {
  return mTriggerLines[index];
}

vector<WorldTriggerLine*> Layer::findTriggerLines(wp::BoundingBox const& bounds) const {
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

void Layer::addTriggerLineToLookupGrid(WorldTriggerLine* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  mTriggerLookupGrid->addItem(triggerLine->getId(), triggerLine->getBounds());
}

void Layer::removeTriggerLineFromLookupGrid(WorldTriggerLine const* triggerLine) {
  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  mTriggerLookupGrid->removeItem(triggerLine->getId());
}

}  // namespace core
}  // namespace bw
