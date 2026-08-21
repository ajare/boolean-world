#include "core/DefinePrefabs.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <limits>
#include <memory>
#include <set>

#include "core/CoreException.h"
#include "core/Defines.h"

namespace bw {
namespace core {

using namespace std;

Prefab::Prefab(uint32_t id, string const& name)
    : mId(id), mName(name) {
}

Prefab::~Prefab() {
  clear();
}

Prefab* Prefab::copy(
    map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const {
  auto clone = unique_ptr<Prefab>(new Prefab(mId, mName));
  clone->mPrimitives.reserve(mPrimitives.size());
  for (auto const* primitive : mPrimitives) {
    auto* clonedPrimitive = primitive->copy();
    primitiveMap[primitive] = clonedPrimitive;
    clone->mPrimitives.push_back(clonedPrimitive);
  }
  return clone.release();
}

uint32_t Prefab::adoptPrimitive(Primitive* primitive) {
  auto const index = (uint32_t)mPrimitives.size();
  mPrimitives.push_back(primitive);
  return index;
}

void Prefab::replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) {
  auto it = find(mPrimitives.begin(), mPrimitives.end(), oldPrimitive);
  if (it == mPrimitives.end()) {
    throw CoreException(format("{} primitive {} not found in Prefab {}",
                               oldPrimitive->getType(), oldPrimitive->getName(), mName));
  }
  if (oldPrimitive == newPrimitive) {
    return;
  }

  delete *it;
  if (newPrimitive) {
    *it = newPrimitive;
  } else {
    mPrimitives.erase(it);
  }
}

bool Prefab::ownsPrimitive(Primitive const* primitive) const {
  return find(mPrimitives.begin(), mPrimitives.end(), primitive) != mPrimitives.end();
}

void Prefab::clear() {
  for (auto* primitive : mPrimitives) {
    delete primitive;
  }
  mPrimitives.clear();
}

uint32_t Prefab::getId() const {
  return mId;
}

string const& Prefab::getName() const {
  return mName;
}

uint32_t Prefab::getNumPrimitives() const {
  return (uint32_t)mPrimitives.size();
}

Primitive* Prefab::getPrimitive(uint32_t index) const {
  assert(index < getNumPrimitives() && "Prefab::getPrimitive(index) - index out of bounds");
  return mPrimitives[index];
}

vector<Primitive*> const& Prefab::getPrimitives() const {
  return mPrimitives;
}

DefinePrefabs::DefinePrefabs()
    : mNextPrefabId(0),
      mTilingType(PrefabTilingType::Square),
      mSize(64.0f),
      mSelectedPrefab(nullptr) {
}

DefinePrefabs::~DefinePrefabs() {
  clear();
}

string DefinePrefabs::getType() const {
  return "DefinePrefabs";
}

bool DefinePrefabs::mayBeFirstStep() const {
  return false;
}

LayerBuildStep* DefinePrefabs::copy(
    map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const {
  auto clone = make_unique<DefinePrefabs>();
  clone->copyFrom(*this);
  clone->mNextPrefabId = mNextPrefabId;
  clone->mTilingType = mTilingType;
  clone->mSize = mSize;
  clone->mPrefabs.reserve(mPrefabs.size());
  for (auto const* prefab : mPrefabs) {
    clone->mPrefabs.push_back(prefab->copy(primitiveMap));
  }
  // mSelectedPrefab deliberately remains null.
  return clone.release();
}

void DefinePrefabs::execute(LayerBuildContext& context) const {
  if (!mSelectedPrefab) {
    return;
  }
  for (auto* primitive : mSelectedPrefab->mPrimitives) {
    context.appendPrimitive(primitive);
  }
}

bool DefinePrefabs::primitivesParticipateInBuild() const {
  return false;
}

bool DefinePrefabs::permitsDirectPrimitiveEditing() const {
  return mSelectedPrefab != nullptr;
}

bool DefinePrefabs::acceptsNewPrimitives() const {
  return mSelectedPrefab != nullptr;
}

uint32_t DefinePrefabs::adoptPrimitive(Primitive* primitive) {
  if (!mSelectedPrefab) {
    throw CoreException("Cannot add a Primitive to DefinePrefabs without a selected Prefab");
  }
  auto const index = mSelectedPrefab->adoptPrimitive(primitive);
  modify();
  return index;
}

void DefinePrefabs::replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) {
  if (!mSelectedPrefab || !mSelectedPrefab->ownsPrimitive(oldPrimitive)) {
    throw CoreException("Primitive not owned by the selected Prefab");
  }
  mSelectedPrefab->replacePrimitive(oldPrimitive, newPrimitive);
  modify();
}

bool DefinePrefabs::ownsPrimitive(Primitive const* primitive) const {
  return mSelectedPrefab && mSelectedPrefab->ownsPrimitive(primitive);
}

Prefab* DefinePrefabs::addPrefab(string const& name) {
  if (mNextPrefabId == numeric_limits<uint32_t>::max()) {
    throw CoreException("No Prefab ids remain in this DefinePrefabs step");
  }

  auto prefab = unique_ptr<Prefab>(new Prefab(mNextPrefabId++, name));
  auto* result = prefab.get();
  mPrefabs.push_back(prefab.release());
  modify();
  return result;
}

void DefinePrefabs::removePrefab(Prefab* prefab, bool failIfNotFound) {
  auto it = find(mPrefabs.begin(), mPrefabs.end(), prefab);
  if (it == mPrefabs.end()) {
    if (failIfNotFound) {
      throw CoreException("Prefab not found in this DefinePrefabs step");
    }
    return;
  }

  if (mSelectedPrefab == prefab) {
    mSelectedPrefab = nullptr;
  }
  delete *it;
  mPrefabs.erase(it);
  modify();
}

void DefinePrefabs::removePrefab(uint32_t index) {
  assert(index < getNumPrefabs() && "DefinePrefabs::removePrefab(index) - index out of bounds");
  removePrefab(mPrefabs[index]);
}

void DefinePrefabs::setPrefabName(Prefab* prefab, string const& name) {
  auto it = find(mPrefabs.begin(), mPrefabs.end(), prefab);
  if (it == mPrefabs.end()) {
    throw CoreException("Prefab not found in this DefinePrefabs step");
  }
  if (prefab->mName == name) {
    return;
  }
  prefab->mName = name;
  modify();
}

uint32_t DefinePrefabs::getNumPrefabs() const {
  return (uint32_t)mPrefabs.size();
}

Prefab* DefinePrefabs::getPrefab(uint32_t index) const {
  assert(index < getNumPrefabs() && "DefinePrefabs::getPrefab(index) - index out of bounds");
  return mPrefabs[index];
}

Prefab* DefinePrefabs::findPrefabById(uint32_t id) const {
  auto it = find_if(mPrefabs.begin(), mPrefabs.end(),
                    [id](auto const* prefab) { return prefab->getId() == id; });
  return it != mPrefabs.end() ? *it : nullptr;
}

vector<Prefab*> const& DefinePrefabs::getPrefabs() const {
  return mPrefabs;
}

void DefinePrefabs::setSelectedPrefab(Prefab* prefab) {
  if (prefab && find(mPrefabs.begin(), mPrefabs.end(), prefab) == mPrefabs.end()) {
    throw CoreException("Cannot select a Prefab from another DefinePrefabs step");
  }
  mSelectedPrefab = prefab;
}

void DefinePrefabs::setSelectedPrefabIndex(uint32_t index) {
  if (index >= getNumPrefabs()) {
    throw CoreException(format("Cannot select Prefab {} in a step with {} Prefabs",
                               index, getNumPrefabs()));
  }
  setSelectedPrefab(mPrefabs[index]);
}

void DefinePrefabs::clearSelectedPrefab() {
  mSelectedPrefab = nullptr;
}

Prefab* DefinePrefabs::getSelectedPrefab() const {
  return mSelectedPrefab;
}

uint32_t DefinePrefabs::getSelectedPrefabIndex() const {
  auto it = find(mPrefabs.begin(), mPrefabs.end(), mSelectedPrefab);
  return it != mPrefabs.end() ? (uint32_t)distance(mPrefabs.begin(), it) : ~0u;
}

void DefinePrefabs::setTilingType(PrefabTilingType type) {
  if (type != PrefabTilingType::Square) {
    throw CoreException("Unknown Prefab tiling type");
  }
  if (mTilingType == type) {
    return;
  }
  mTilingType = type;
  modify();
}

PrefabTilingType DefinePrefabs::getTilingType() const {
  return mTilingType;
}

void DefinePrefabs::setSize(float size) {
  if (mSize == size) {
    return;
  }
  mSize = size;
  modify();
}

float DefinePrefabs::getSize() const {
  return mSize;
}

bool DefinePrefabs::childrenModified() const {
  for (auto const* prefab : mPrefabs) {
    if (any_of(prefab->mPrimitives.begin(), prefab->mPrimitives.end(),
               [](auto const* primitive) { return primitive->isModified(); })) {
      return true;
    }
  }
  return false;
}

void DefinePrefabs::serializeArgs(shared_ptr<Serializer> serializer,
                                  SerializationWorkData& workData) const {
  serializer->writeUint32("nextPrefabId", mNextPrefabId);
  serializer->writeUint32("tilingType", (uint32_t)mTilingType);
  serializer->writeFloat("size", mSize);
  serializer->beginArray("prefabs");
  {
    for (auto const* prefab : mPrefabs) {
      serializer->beginMap("prefab");
      {
        serializer->writeUint32("id", prefab->mId);
        serializer->writeString("name", prefab->mName);
        serializer->beginArray("primitives");
        {
          for (auto const* primitive : prefab->mPrimitives) {
            if (!workData.includeGhostPrimitives &&
                (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) != 0) {
              continue;
            }
            serializer->beginMap("primitive");
            {
              serializer->writeString("type", primitive->getType());
              primitive->serialize(serializer, workData);
              serializer->endMap();
            }
          }
          serializer->endArray();
        }
        serializer->endMap();
      }
    }
    serializer->endArray();
  }
}

bool DefinePrefabs::deserializeArgs(shared_ptr<Serializer> serializer,
                                    SerializationWorkData& workData) {
  auto const nextPrefabId = serializer->readUint32("nextPrefabId");
  auto const tilingType = (PrefabTilingType)serializer->readUint32("tilingType");
  auto const size = serializer->readFloat("size");
  if (tilingType != PrefabTilingType::Square) {
    throw CoreException("Unknown Prefab tiling type in DefinePrefabs step");
  }

  vector<unique_ptr<Prefab>> prefabs;
  set<uint32_t> ids;
  serializer->beginArray("prefabs");
  {
    while (serializer->nextArrayItem()) {
      serializer->beginMap("prefab");
      {
        auto const id = serializer->readUint32("id");
        auto prefab = unique_ptr<Prefab>(new Prefab(id, serializer->readString("name")));
        if (!ids.insert(id).second || id >= nextPrefabId) {
          throw CoreException("Invalid or duplicate Prefab id in DefinePrefabs step");
        }

        serializer->beginArray("primitives");
        {
          while (serializer->nextArrayItem()) {
            serializer->beginMap("primitive");
            {
              auto primitive = unique_ptr<Primitive>(
                  Primitive::instantiate(serializer->readString("type")));
              if (!primitive->deserialize(serializer, workData)) {
                copyErrorsAndWarnings(primitive.get(), true, true);
                return false;
              }
              prefab->adoptPrimitive(primitive.release());
              serializer->endMap();
            }
          }
          serializer->endArray();
        }
        prefabs.push_back(move(prefab));
        serializer->endMap();
      }
    }
    serializer->endArray();
  }

  clear();
  mNextPrefabId = nextPrefabId;
  mTilingType = tilingType;
  mSize = size;
  mPrefabs.reserve(prefabs.size());
  for (auto& prefab : prefabs) {
    mPrefabs.push_back(prefab.release());
  }
  mSelectedPrefab = nullptr;
  return true;
}

void DefinePrefabs::clear() {
  for (auto* prefab : mPrefabs) {
    delete prefab;
  }
  mPrefabs.clear();
  mSelectedPrefab = nullptr;
}

}  // namespace core
}  // namespace bw
