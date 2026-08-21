#include <map>
#include <memory>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <set>

#include "core/Layer.h"
#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/LayerBuildStep.h"
#include "core/PrimitiveField.h"

namespace bw {
namespace core {

using namespace std;

LayerBuildContext::LayerBuildContext(
    Layer& layer,
    LayerBuildStep const* step,
    vector<Primitive*> const& buildPrimitives)
    : mLayer(layer), mStep(step), mBuildPrimitives(buildPrimitives) {
}

vector<Primitive*> const& LayerBuildContext::getBuildPrimitives() const {
  return mBuildPrimitives;
}

uint32_t LayerBuildContext::appendPrimitive(Primitive* primitive) {
  return mLayer._appendBuiltPrimitive(primitive, mStep);
}

Layer::Layer()
    : Layer(0, "", BW_WORLD_SIZE, -1.0f) {
}

Layer::Layer(uint32_t id, string const& name, float size, float gridSize)
    : mId(id), mNextStepId(0), mName(name), mExtents(-size / 2, -size / 2, size, size), mActiveStepIndex(0), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr), mFrameNumber(0) {
  mPrimitiveCellMetadataUpdater = bind(&Layer::updatePrimitiveCellMetadata, this, placeholders::_1);

  seedFirstStep();

  if (gridSize > 0.0f) {
    createAccelerationGrids(gridSize);
  }
}

Layer::Layer(Layer const& other)
    : mNextStepId(0), mActiveStepIndex(0), mPrimitiveLookupGrid(nullptr), mTriggerLookupGrid(nullptr), mFrameNumber(0) {
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
  swap(mNextStepId, other.mNextStepId);
  swap(mName, other.mName);
  swap(mExtents, other.mExtents);
  swap(mSteps, other.mSteps);
  swap(mActiveStepIndex, other.mActiveStepIndex);
  swap(mPrimitives, other.mPrimitives);
  swap(mPrimitiveSteps, other.mPrimitiveSteps);
  swap(mTriggerLines, other.mTriggerLines);
  swap(mPrimitiveLookupGrid, other.mPrimitiveLookupGrid);
  swap(mTriggerLookupGrid, other.mTriggerLookupGrid);
  swap(mFrameNumber, other.mFrameNumber);
}

void Layer::rebindOwnedState() {
  for (auto* primitive : mPrimitives) {
    primitive->mWorld = nullptr;
    primitive->mInputs.triggerLines = &mTriggerLines;
  }
}

Layer::~Layer() {
  teardown();
}

void Layer::copyFrom(Layer const& other) {
  Serializable::copyFrom(other);

  mId = other.mId;
  mNextStepId = other.mNextStepId;
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

  // Clone every step, recording each cloned Primitive against the one it came
  // from, so parent links can then be remapped as a complete graph regardless
  // of which step a parent happens to live in.
  map<VertexTransformerObject const*, VertexTransformerObject*> primitiveMap;

  deleteSteps();
  mSteps.reserve(other.mSteps.size());
  for (auto const* step : other.mSteps) {
    auto* clone = step->copy(primitiveMap);
    clone->setId(step->getId());
    mSteps.push_back(clone);
  }

  for (auto const& [source, clone] : primitiveMap) {
    auto const parent = primitiveMap.find(source->mParent);
    clone->mParent = parent != primitiveMap.end() ? parent->second : nullptr;
  }

  rebuild();
  rebindOwnedState();
}

void Layer::seedFirstStep() {
  assert(mSteps.empty() && "Layer::seedFirstStep - the Layer already has steps");

  auto* step = new PrimitiveField;
  assignStepId(step);
  mSteps.push_back(step);
}

void Layer::assignStepId(LayerBuildStep* step) {
  if (mNextStepId == numeric_limits<uint32_t>::max()) {
    throw CoreException("No LayerBuildStep ids remain in this Layer");
  }

  step->setId(mNextStepId++);
}

void Layer::deleteSteps() {
  for (auto* step : mSteps) {
    delete step;
  }

  mSteps.clear();
}

bool Layer::childrenModified() const {
  for (auto const* step : mSteps) {
    if (step->isModified()) {
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
    serializer->writeUint32("nextStepId", mNextStepId);

    // Only the recipe is written; the Primitives it produces are derived on
    // load by rebuilding from it (docs/adr/0014).
    serializer->beginArray("steps");
    {
      for (auto const* step : mSteps) {
        serializer->beginMap("step");
        {
          serializer->writeString("type", step->getType());

          step->serialize(serializer, workData);

          serializer->endMap();  // step
        }
      }

      serializer->endArray();  // steps
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
  uint32_t nextStepId;
  vector<unique_ptr<LayerBuildStep>> steps;
  vector<unique_ptr<WorldTriggerLine>> triggerLines;

  try {
    serializer->beginMap("layer");
    {
      id = serializer->readUint32("id");
      name = serializer->readString("name", true);
      minExtent = serializer->readVector2("minExtent");
      maxExtent = serializer->readVector2("maxExtent");
      nextStepId = serializer->readUint32(
          "nextStepId", !serializer->isPositional(), ~0u);

      serializer->beginArray("steps");
      {
        while (serializer->nextArrayItem()) {
          serializer->beginMap("step");
          {
            auto stepType = serializer->readString("type");

            auto step = unique_ptr<LayerBuildStep>(LayerBuildStep::instantiate(stepType));

            if (!step->deserialize(serializer, workData)) {
              copyErrorsAndWarnings(step.get(), true, true);
              return false;
            }

            steps.push_back(move(step));

            serializer->endMap();  // step
          }
        }

        serializer->endArray();  // steps
      }

      if (steps.empty() || !steps.front()->mayBeFirstStep()) {
        throw CoreException("A Layer's first build step must be a PrimitiveField step");
      }

      if (nextStepId == ~0u) {
        // Files saved before LayerBuildStep ids existed have no references to
        // preserve, so assign their recipe-order ids as they are first loaded.
        nextStepId = (uint32_t)steps.size();
        for (uint32_t i = 0; i < steps.size(); ++i) {
          steps[i]->setId(i);
        }
      } else {
        set<uint32_t> stepIds;
        for (auto const& step : steps) {
          if (step->getId() == ~0u || !stepIds.insert(step->getId()).second ||
              step->getId() >= nextStepId) {
            throw CoreException("Invalid or duplicate LayerBuildStep id in Layer");
          }
        }
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
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mId = id;
  mNextStepId = nextStepId;
  mName = name;
  mExtents.setPosition(minExtent);
  mExtents.setSize(maxExtent - minExtent);
  mActiveStepIndex = 0;

  mPrimitives.clear();
  mPrimitiveSteps.clear();
  rebuildAccelerationGrids(workData.accelGridSize);

  // Add TriggerLines before the steps run, as the Primitives they produce may
  // need them for their initial values.
  for (auto& triggerLine : triggerLines) {
    addTriggerLine(triggerLine.get());
    triggerLine.release();
  }

  deleteSteps();
  mSteps.reserve(steps.size());
  for (auto& step : steps) {
    mSteps.push_back(step.release());
  }

  rebuild();
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

void Layer::rebuildAccelerationGrids(float targetCellSize) {
  if (targetCellSize > 0.0f) {
    delete mPrimitiveLookupGrid;
    mPrimitiveLookupGrid = nullptr;
    delete mTriggerLookupGrid;
    mTriggerLookupGrid = nullptr;

    createAccelerationGrids(targetCellSize);

    return;
  }

  if (mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid->removeAllItems(mPrimitiveCellMetadataUpdater);
  }

  if (mTriggerLookupGrid) {
    mTriggerLookupGrid->removeAllItems();
  }
}

float Layer::getPrimitiveAccelerationGridSize() const {
  return mPrimitiveLookupGrid ? mPrimitiveLookupGrid->getCellSize().x : -1.0f;
}

bool Layer::getGridSettings(int* dimX, int* dimY, float* cellSize) const {
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

void Layer::getGridCellFrameNumber(uint32_t cellIndex, frame_number_type* frameNumber) const {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  *frameNumber = mPrimitiveLookupGrid->getUser(cellIndex).lastUpdatedFrameNumber;
}

void Layer::_setFrameNumber(frame_number_type frameNumber) {
  mFrameNumber = frameNumber;
}

void Layer::clear() {
  teardown();

  seedFirstStep();
}

void Layer::teardown() {
  delete mPrimitiveLookupGrid;
  mPrimitiveLookupGrid = nullptr;

  delete mTriggerLookupGrid;
  mTriggerLookupGrid = nullptr;

  // The derived Primitives are owned by the steps, so dropping the steps is
  // what destroys them.
  mPrimitives.clear();
  mPrimitiveSteps.clear();
  deleteSteps();

  for (auto triggerLine : mTriggerLines) {
    delete triggerLine;
  }

  mTriggerLines.clear();
}

uint32_t Layer::getId() const {
  return mId;
}

void Layer::_setId(uint32_t id) {
  mId = id;
}

void Layer::setName(string const& name) {
  mName = name;
}

string const& Layer::getName() const {
  return mName;
}

void Layer::setExtents(wp::BoundingBox const& extents) {
  mExtents = extents;
}

wp::BoundingBox const& Layer::getExtents() const {
  return mExtents;
}

void Layer::updatePrimitiveCellMetadata(PrimitiveCellMetadata* metadata) {
  metadata->lastUpdatedFrameNumber = max(metadata->lastUpdatedFrameNumber, mFrameNumber);
}

void Layer::rebuild() {
  if (mPrimitiveLookupGrid) {
    mPrimitiveLookupGrid->removeAllItems(mPrimitiveCellMetadataUpdater);
  }

  mPrimitives.clear();
  mPrimitiveSteps.clear();

  for (auto const* step : mSteps) {
    if (step->isEnabled()) {
      vector<Primitive*> buildPrimitives;
      buildPrimitives.reserve(mPrimitives.size());
      for (uint32_t i = 0; i < mPrimitives.size(); ++i) {
        if (mPrimitiveSteps[i]->primitivesParticipateInBuild()) {
          buildPrimitives.push_back(mPrimitives[i]);
        }
      }

      LayerBuildContext context(*this, step, buildPrimitives);
      step->execute(context);
    }
  }
}

uint32_t Layer::getNumSteps() const {
  return (uint32_t)mSteps.size();
}

LayerBuildStep* Layer::getStep(uint32_t index) const {
  assert(index < getNumSteps() && "Layer::getStep(index) - index out of bounds");

  return mSteps[index];
}

PrimitiveField* Layer::getPrimitiveField() const {
  assert(!mSteps.empty() && "Layer::getPrimitiveField - the Layer has no first step");

  return static_cast<PrimitiveField*>(mSteps.front());
}

uint32_t Layer::getActiveStepIndex() const {
  return mActiveStepIndex;
}

LayerBuildStep* Layer::getActiveStep() const {
  return mSteps[mActiveStepIndex];
}

void Layer::setActiveStep(uint32_t index) {
  if (index >= getNumSteps()) {
    throw CoreException(format("Cannot make step {} active in a Layer with {} steps", index, getNumSteps()));
  }

  mActiveStepIndex = index;
}

uint32_t Layer::insertStep(uint32_t index, LayerBuildStep* step) {
  if (index == 0) {
    throw CoreException("Index 0 is reserved for a Layer's PrimitiveField step; steps may only be inserted at index 1 or above");
  }

  if (index > getNumSteps()) {
    throw CoreException(format("Cannot insert a build step at index {} into a Layer with {} steps", index, getNumSteps()));
  }

  assignStepId(step);
  mSteps.insert(mSteps.begin() + index, step);

  // Inserting at or before the active step's index shifts it along with
  // everything else that was there.
  if (index <= mActiveStepIndex) {
    mActiveStepIndex++;
  }

  rebuild();

  return index;
}

uint32_t Layer::addStep(LayerBuildStep* step) {
  return insertStep(getNumSteps(), step);
}

void Layer::removeStep(uint32_t index) {
  if (index == 0) {
    throw CoreException("A Layer's first build step cannot be deleted; it can only be disabled");
  }

  if (index >= getNumSteps()) {
    throw CoreException(format("Cannot remove build step {} from a Layer with {} steps", index, getNumSteps()));
  }

  delete mSteps[index];
  mSteps.erase(mSteps.begin() + index);

  // Keep the active step's identity stable across the removal: if it was
  // the one removed, fall back to the first step; otherwise track its new
  // position, mirroring World::removeLayer's active-Layer handling.
  if (mActiveStepIndex == index) {
    mActiveStepIndex = 0;
  } else if (mActiveStepIndex > index) {
    mActiveStepIndex--;
  }

  rebuild();
}

void Layer::moveStep(uint32_t fromIndex, uint32_t toIndex) {
  if (fromIndex == 0 || toIndex == 0) {
    throw CoreException("A Layer's first build step is pinned at index 0 and cannot be moved, and no other step may move into index 0");
  }

  if (fromIndex >= getNumSteps() || toIndex >= getNumSteps()) {
    throw CoreException(format("Cannot move build step {} to {} in a Layer with {} steps", fromIndex, toIndex, getNumSteps()));
  }

  if (fromIndex == toIndex) {
    return;
  }

  auto* activeStep = mSteps[mActiveStepIndex];

  auto* step = mSteps[fromIndex];
  mSteps.erase(mSteps.begin() + fromIndex);
  mSteps.insert(mSteps.begin() + toIndex, step);

  auto it = find(mSteps.begin(), mSteps.end(), activeStep);
  mActiveStepIndex = (uint32_t)distance(mSteps.begin(), it);

  rebuild();
}

void Layer::setStepEnabled(uint32_t index, bool enabled) {
  assert(index < getNumSteps() && "Layer::setStepEnabled(index) - index out of bounds");

  if (mSteps[index]->isEnabled() == enabled) {
    return;
  }

  mSteps[index]->setEnabled(enabled);

  rebuild();
}

bool Layer::isLastEnabledStep(LayerBuildStep const* step) const {
  auto it = find(mSteps.begin(), mSteps.end(), step);
  if (it == mSteps.end()) {
    return false;
  }

  return none_of(it + 1, mSteps.end(), [](auto const* later) { return later->isEnabled(); });
}

bool Layer::hasContiguousTailOutput(LayerBuildStep const* step) const {
  auto first = find(mPrimitiveSteps.begin(), mPrimitiveSteps.end(), step);
  return first == mPrimitiveSteps.end() ||
         all_of(first, mPrimitiveSteps.end(), [step](auto const* owner) {
           return owner == step;
         });
}

LayerBuildStep* Layer::findOwningStep(Primitive const* primitive) const {
  auto index = getOwningStepIndex(primitive);

  return index != ~0u && mSteps[index]->ownsPrimitive(primitive) ? mSteps[index] : nullptr;
}

uint32_t Layer::getOwningStepIndex(Primitive const* primitive) const {
  auto it = find(mPrimitives.begin(), mPrimitives.end(), primitive);
  if (it == mPrimitives.end()) {
    return ~0u;
  }

  auto primitiveIndex = (uint32_t)distance(mPrimitives.begin(), it);
  auto const* owningStep = mPrimitiveSteps[primitiveIndex];
  auto step = find(mSteps.begin(), mSteps.end(), owningStep);
  return step != mSteps.end() ? (uint32_t)distance(mSteps.begin(), step) : ~0u;
}

uint32_t Layer::_appendBuiltPrimitive(Primitive* primitive, LayerBuildStep const* owningStep) {
  assert(owningStep && "Layer::_appendBuiltPrimitive requires the producing step");

  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  auto index = (uint32_t)mPrimitives.size();

  mPrimitives.push_back(primitive);
  mPrimitiveSteps.push_back(owningStep);

  primitive->setId(index);
  primitive->setInputs(wp::Vector2::ZERO, 0.0f, &mTriggerLines);

  addPrimitiveToLookupGrid(primitive);

  primitive->_invalidate();

  return index;
}

uint32_t Layer::addPrimitive(Primitive* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  if (getNumPrimitives() >= BW_WORLD_PRIMITIVE_COUNT_MAX) {
    throw CoreException("Too many primitives added to the Layer");
  }

  auto* activeStep = getActiveStep();
  if (!activeStep->acceptsNewPrimitives()) {
    throw CoreException("Cannot add a Primitive: the active step does not accept new Primitives");
  }

  if (!activeStep->isEnabled()) {
    throw CoreException("Cannot add a Primitive while the active step is disabled");
  }

  activeStep->adoptPrimitive(primitive);

  // Nothing enabled runs after this step, so the new Primitive lands at the
  // end of the derived collection and the cache can simply be extended.
  if (isLastEnabledStep(activeStep)) {
    assert(hasContiguousTailOutput(activeStep) &&
           "in-place add requires the active step's output to be a contiguous tail");
    return _appendBuiltPrimitive(primitive, activeStep);
  }

  rebuild();

  auto it = find(mPrimitives.begin(), mPrimitives.end(), primitive);
  return (uint32_t)distance(mPrimitives.begin(), it);
}

void Layer::removePrimitive(Primitive* primitive, bool failIfNotFound) {
  auto* step = findOwningStep(primitive);

  if (!step) {
    if (failIfNotFound) {
      throw CoreException(format("{} primitive {} not found in layer", primitive->getType(), primitive->getName()));
    }

    return;
  }

  // The storage interface has no release operation: the owning step
  // destroys the Primitive as part of replacement with no successor.
  step->replacePrimitive(primitive, nullptr);
  rebuild();
}

void Layer::removePrimitive(uint32_t index) {
  assert(index < getNumPrimitives() && "Layer::removePrimitive(index) - index out of bounds");

  removePrimitive(mPrimitives[index]);
}

void Layer::removePrimitives(vector<uint32_t> const& indices) {
  // Resolve every index up front: each removal rebuilds the derived
  // collection, so indices taken against the original one go stale.
  vector<Primitive*> targets;
  targets.reserve(indices.size());
  for (auto index : indices) {
    if (index < getNumPrimitives()) {
      targets.push_back(mPrimitives[index]);
    }
  }

  uint32_t numDeleted{0};
  for (auto* target : targets) {
    // A repeated index resolves to a Primitive already removed, which leaves
    // numDeleted short and is reported below.
    auto* step = findOwningStep(target);
    if (!step) {
      continue;
    }

    step->replacePrimitive(target, nullptr);
    numDeleted++;
  }

  rebuild();

  if (numDeleted != (uint32_t)indices.size()) {
    throw CoreException("Could not delete all selected primitives");
  }
}

void Layer::replacePrimitive(uint32_t index, Primitive* newPrimitive, bool failIfNotFound) {
  assert(index < getNumPrimitives() && "Layer::replacePrimitive(index, newPrimitive) - index out of bounds");

  auto* oldPrimitive = mPrimitives[index];
  auto* step = findOwningStep(oldPrimitive);

  if (!step) {
    if (failIfNotFound) {
      throw CoreException(format("{} primitive {} not found in layer", oldPrimitive->getType(), oldPrimitive->getName()));
    }

    return;
  }

  try {
    removePrimitiveFromLookupGrid(oldPrimitive, failIfNotFound);
  } catch (exception const&) {
    if (failIfNotFound) {
      throw;
    }
  }

  step->replacePrimitive(oldPrimitive, newPrimitive);

  // A replacement changes neither the number of Primitives nor their order,
  // so as long as no enabled step runs after this one the cache stays valid
  // with the new Primitive slotted into the old one's place.
  if (!isLastEnabledStep(step)) {
    rebuild();
    return;
  }

  assert(hasContiguousTailOutput(step) &&
         "in-place replacement requires the owning step's output to be a contiguous tail");

  mPrimitives[index] = newPrimitive;

  newPrimitive->setId(index);
  newPrimitive->setInputs(wp::Vector2::ZERO, 0.0f, &mTriggerLines);
  addPrimitiveToLookupGrid(newPrimitive);
}

void Layer::primitiveChanged(Primitive const* primitive) {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

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

vector<uint32_t> Layer::getPrimitiveIndicesInGridCell(uint32_t cellIndex) const {
  if (!mPrimitiveLookupGrid) {
    throw CoreException("AccelerationGrid for primitives not created.");
  }

  auto const& primIndices = mPrimitiveLookupGrid->_getCellItemIndices(cellIndex);

  return {primIndices.begin(), primIndices.end()};
}

vector<uint32_t> Layer::getPrimitiveCandidateIndices(wp::Vector2 const& worldPos) const {
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
  auto numTriggerLines = (uint32_t)mTriggerLines.size();
  for (uint32_t i = 0; i < numTriggerLines; ++i) {
    if (mTriggerLines[i] != triggerLine) {
      continue;
    }

    removeTriggerLineFromLookupGrid(triggerLine);
    delete triggerLine;
    for (uint32_t j = i; j < numTriggerLines - 1; ++j) {
      auto* shifted = mTriggerLines[j + 1];
      removeTriggerLineFromLookupGrid(shifted);
      mTriggerLines[j] = shifted;
      shifted->setId(j);
      addTriggerLineToLookupGrid(shifted);
    }

    mTriggerLines.pop_back();
    return;
  }

  if (failIfNotFound) {
    throw CoreException("WorldTriggerLine not found.");
  }
}

void Layer::removeTriggerLine(uint32_t index) {
  assert(index < getNumTriggerLines() && "Layer::removeTriggerLine(index) - index out of bounds");

  removeTriggerLine(mTriggerLines[index]);
}

void Layer::removeTriggerLines(vector<uint32_t> const& indices) {
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

void Layer::replaceTriggerLine(uint32_t index, WorldTriggerLine* newTriggerLine, bool failIfNotFound) {
  assert(index < getNumTriggerLines() && "Layer::replaceTriggerLine(index, newTriggerLine) - index out of bounds");

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

void Layer::setTriggerLinePoints(uint32_t triggerLineIndex, wp::Vector2 const& p0, wp::Vector2 const& p1) {
  assert(triggerLineIndex < getNumTriggerLines() && "Layer::setTriggerLinePoints - trigger line index out of bounds");

  if (!mTriggerLookupGrid) {
    throw CoreException("AccelerationGrid for TriggerLines not created.");
  }

  auto* triggerLine = mTriggerLines[triggerLineIndex];
  triggerLine->setPoints(p0, p1);
  mTriggerLookupGrid->moveItem(triggerLineIndex, triggerLine->getBounds());
}

uint32_t Layer::getNumTriggerLines() const {
  return (uint32_t)mTriggerLines.size();
}

vector<WorldTriggerLine*> const& Layer::getTriggerLines() const {
  return mTriggerLines;
}

vector<WorldTriggerLine*>* Layer::_getTriggerLineStorage() {
  return &mTriggerLines;
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
