#include "core/PrefabField.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/CoreException.h"
#include "core/Layer.h"

namespace bw::core {
using namespace std;

string PrefabField::getType() const { return "PrefabField"; }
bool PrefabField::mayBeFirstStep() const { return false; }

LayerBuildStep* PrefabField::copy(map<VertexTransformerObject const*, VertexTransformerObject*>&) const {
  auto* result = new PrefabField;
  result->copyFrom(*this);
  result->mDefinePrefabsStepId = mDefinePrefabsStepId;
  result->mInstances = mInstances;
  return result;
}

void PrefabField::execute(LayerBuildContext& context) const {
  mBuiltPrimitives.clear();
  auto* definitions = dynamic_cast<DefinePrefabs*>(context.getLayer().getStepById(mDefinePrefabsStepId));
  if (!definitions) return;

  for (auto const& [tile, instance] : mInstances) {
    auto* prefab = definitions->findPrefabById(instance.prefabId);
    if (!prefab) continue;
    auto const angles = prefabTilingRotationAngles(definitions->getTilingType());
    auto angle = instance.rotation < angles.size() ? angles[instance.rotation] : 0.0f;
    auto offset = wp::Vector2{tile.x * definitions->getSize(), tile.y * definitions->getSize()};
    map<VertexTransformerObject const*, VertexTransformerObject*> clones;
    for (auto const* source : prefab->getPrimitives()) {
      unique_ptr<Primitive> clone(source->rotatedCopy(angle));
      clone->setPosition(clone->getPosition() + offset);
      clones[source] = clone.get();
      mBuiltPrimitives.push_back(move(clone));
    }
    for (auto const* source : prefab->getPrimitives()) {
      auto* parent = source->getParent();
      if (parent && clones.contains(parent))
        clones[source]->setParent(clones[parent]);
      else
        clones[source]->setParent(nullptr);
    }
  }
  for (auto const& primitive : mBuiltPrimitives) context.appendPrimitive(primitive.get());
}

bool PrefabField::primitivesParticipateInBuild() const { return true; }
bool PrefabField::permitsDirectPrimitiveEditing() const { return false; }
bool PrefabField::acceptsNewPrimitives() const { return false; }
uint32_t PrefabField::adoptPrimitive(Primitive*) { throw CoreException("PrefabField does not accept Primitives"); }
void PrefabField::replacePrimitive(Primitive*, Primitive*) { throw CoreException("PrefabField output cannot be edited directly"); }
bool PrefabField::ownsPrimitive(Primitive const* primitive) const {
  return any_of(mBuiltPrimitives.begin(), mBuiltPrimitives.end(), [primitive](auto const& item) { return item.get() == primitive; });
}

void PrefabField::bind(Layer const& layer, DefinePrefabs const* step) {
  if (step) {
    auto* owned = layer.getStepById(step->getId());
    if (owned != step) throw CoreException("PrefabField cannot bind to a DefinePrefabs step on another Layer");
  }
  mDefinePrefabsStepId = step ? step->getId() : ~0u;
  mSelectedPrefabId = ~0u;
  modify();
}
uint32_t PrefabField::getDefinePrefabsStepId() const { return mDefinePrefabsStepId; }
DefinePrefabs* PrefabField::getDefinePrefabs(Layer const& layer) const {
  return dynamic_cast<DefinePrefabs*>(layer.getStepById(mDefinePrefabsStepId));
}
void PrefabField::setSelectedPrefab(DefinePrefabs const& definitions, Prefab const* prefab) {
  if (definitions.getId() != mDefinePrefabsStepId || (prefab && definitions.findPrefabById(prefab->getId()) != prefab))
    throw CoreException("Cannot select a Prefab outside this PrefabField's binding");
  mSelectedPrefabId = prefab ? prefab->getId() : ~0u;
}
void PrefabField::clearSelectedPrefab() { mSelectedPrefabId = ~0u; }
Prefab* PrefabField::getSelectedPrefab(Layer const& layer) const {
  auto* definitions = getDefinePrefabs(layer);
  return definitions ? definitions->findPrefabById(mSelectedPrefabId) : nullptr;
}
void PrefabField::selectTile(Tile tile) {
  mSelectedTile = tile;
  mHasSelectedTile = true;
}
void PrefabField::clearSelectedTile() { mHasSelectedTile = false; }
bool PrefabField::hasSelectedTile() const { return mHasSelectedTile; }
Tile PrefabField::getSelectedTile() const { return mSelectedTile; }
Tile PrefabField::tileAt(Layer const& layer, wp::Vector2 const& position) const {
  auto* definitions = getDefinePrefabs(layer);
  if (!definitions) throw CoreException("Cannot address a tile in an unbound PrefabField");
  auto size = definitions->getSize();
  return {(int32_t)floor(position.x / size + 0.5f), (int32_t)floor(position.y / size + 0.5f)};
}
bool PrefabField::placeSelected(Layer& layer, Tile tile) {
  if (!getSelectedPrefab(layer)) return false;
  selectTile(tile);
  PrefabInstance replacement{mSelectedPrefabId, 0};
  auto it = mInstances.find(tile);
  if (it != mInstances.end() && it->second.prefabId == replacement.prefabId && it->second.rotation == 0) return false;
  mInstances[tile] = replacement;
  modify();
  layer.rebuild();
  return true;
}
bool PrefabField::clearInstance(Layer& layer, Tile tile) {
  if (!mInstances.erase(tile)) return false;
  modify();
  layer.rebuild();
  return true;
}
bool PrefabField::rotateInstance(Layer& layer, Tile tile, bool next) {
  auto it = mInstances.find(tile);
  auto* definitions = getDefinePrefabs(layer);
  if (it == mInstances.end() || !definitions) return false;

  auto const angles = prefabTilingRotationAngles(definitions->getTilingType());
  if (angles.empty()) return false;
  auto const count = static_cast<uint32_t>(angles.size());
  auto const current = it->second.rotation % count;
  it->second.rotation = next ? (current + 1) % count : (current + count - 1) % count;
  modify();
  layer.rebuild();
  return true;
}
PrefabInstance const* PrefabField::getInstance(Tile tile) const {
  auto it = mInstances.find(tile);
  return it == mInstances.end() ? nullptr : &it->second;
}
map<Tile, PrefabInstance> const& PrefabField::getInstances() const { return mInstances; }
bool PrefabField::referencesPrefab(uint32_t prefabId) const {
  return any_of(mInstances.begin(), mInstances.end(), [prefabId](auto const& item) { return item.second.prefabId == prefabId; });
}

void PrefabField::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeUint32("definePrefabsStepId", mDefinePrefabsStepId);
  serializer->beginArray("instances");
  for (auto const& [tile, instance] : mInstances) {
    serializer->beginMap("instance");
    serializer->writeInt32("x", tile.x);
    serializer->writeInt32("y", tile.y);
    serializer->writeUint32("prefabId", instance.prefabId);
    serializer->writeUint32("rotation", instance.rotation);
    serializer->endMap();
  }
  serializer->endArray();
}
bool PrefabField::deserializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) {
  auto stepId = serializer->readUint32("definePrefabsStepId");
  map<Tile, PrefabInstance> instances;
  serializer->beginArray("instances");
  while (serializer->nextArrayItem()) {
    serializer->beginMap("instance");
    Tile tile{serializer->readInt32("x"), serializer->readInt32("y")};
    PrefabInstance instance{serializer->readUint32("prefabId"), serializer->readUint32("rotation")};
    if (!instances.emplace(tile, instance).second) throw CoreException("Duplicate Tile in PrefabField");
    serializer->endMap();
  }
  serializer->endArray();
  mDefinePrefabsStepId = stepId;
  mInstances = move(instances);
  mSelectedPrefabId = ~0u;
  mHasSelectedTile = false;
  mBuiltPrimitives.clear();
  return true;
}
}  // namespace bw::core
