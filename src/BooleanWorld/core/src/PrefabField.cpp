#include "core/PrefabField.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <tuple>

#include "core/CoreException.h"
#include "core/Layer.h"
#include "core/RectanglePolygon.h"

namespace bw::core {
using namespace std;

namespace {
constexpr uint8_t phase256Content = 0;
constexpr uint8_t phase128Difference = 1;
constexpr uint8_t phase128Content = 2;
constexpr uint8_t phase64Difference = 3;
constexpr uint8_t phase64Content = 4;
constexpr uint8_t phase32Difference = 5;
constexpr uint8_t phase32Content = 6;

uint8_t differencePhase(PrefabTileSize size) {
  switch (size) {
    case PrefabTileSize::Size128: return phase128Difference;
    case PrefabTileSize::Size64: return phase64Difference;
    case PrefabTileSize::Size32: return phase32Difference;
    case PrefabTileSize::Size256: break;
  }
  throw CoreException("The 256 Prefab grid has no Replace phase");
}

uint8_t contentPhase(PrefabTileSize size) {
  switch (size) {
    case PrefabTileSize::Size256: return phase256Content;
    case PrefabTileSize::Size128: return phase128Content;
    case PrefabTileSize::Size64: return phase64Content;
    case PrefabTileSize::Size32: return phase32Content;
  }
  throw CoreException("Unknown Prefab tile size");
}

wp::Vector2 tileCentre(Tile tile) {
  auto const side = static_cast<float>(prefabTileSide(tile.size));
  return {(static_cast<float>(tile.x) + 0.5f) * side,
          (static_cast<float>(tile.y) + 0.5f) * side};
}
}  // namespace

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
  mHiddenPrimitives.clear();
  auto* definitions = dynamic_cast<DefinePrefabs*>(
      context.getLayer().getStepById(mDefinePrefabsStepId));
  if (!definitions) return;

  auto appendReplaceSquares = [&](PrefabTileSize size) {
    auto const side = static_cast<float>(prefabTileSide(size));
    for (auto const& [tile, instance] : mInstances) {
      if (tile.size != size || instance.mode != TileMode::Replace) continue;
      auto* prefab = definitions->findPrefabById(instance.prefabId);
      if (!prefab) throw CoreException("PrefabField references an unknown Prefab");
      if (prefab->getTileSize() != size)
        throw CoreException("PrefabField Tile size does not match its Prefab");
      auto square = make_unique<RectanglePolygon>(
          Primitive::Operation::Difference, Primitive::FillRule::NonZero, 1.0f);
      square->setSize(side, side);
      square->setPosition(tileCentre(tile));
      auto* raw = square.get();
      mHiddenPrimitives.push_back(raw);
      mBuiltPrimitives.push_back(move(square));
      context.appendPrimitive(raw, differencePhase(size), 0);
    }
  };

  struct OrderedClone {
    uint8_t sourcePriority;
    uint64_t sequence;
    unique_ptr<Primitive> primitive;
  };

  auto appendContent = [&](PrefabTileSize size) {
    vector<OrderedClone> clones;
    uint64_t sequence = 0;
    for (auto const& [tile, instance] : mInstances) {
      if (tile.size != size) continue;
      auto* prefab = definitions->findPrefabById(instance.prefabId);
      if (!prefab) throw CoreException("PrefabField references an unknown Prefab");
      if (prefab->getTileSize() != size)
        throw CoreException("PrefabField Tile size does not match its Prefab");
      auto const angles = prefabTilingRotationAngles(definitions->getTilingType());
      auto const angle = instance.rotation < angles.size()
                             ? angles[instance.rotation]
                             : 0.0f;
      map<VertexTransformerObject const*, VertexTransformerObject*> instanceClones;
      vector<OrderedClone> instanceOutput;
      instanceOutput.reserve(prefab->getNumPrimitives());
      for (auto const* source : prefab->getPrimitives()) {
        unique_ptr<Primitive> clone(source->rotatedCopy(angle));
        clone->setPosition(clone->getPosition() + tileCentre(tile));
        instanceClones[source] = clone.get();
        instanceOutput.push_back({source->getPriority(), sequence++, move(clone)});
      }
      for (size_t index = 0; index < prefab->getPrimitives().size(); ++index) {
        auto const* source = prefab->getPrimitives()[index];
        auto* parent = source->getParent();
        instanceOutput[index].primitive->setParent(
            parent && instanceClones.contains(parent) ? instanceClones[parent]
                                                      : nullptr);
      }
      move(instanceOutput.begin(), instanceOutput.end(), back_inserter(clones));
    }
    stable_sort(clones.begin(), clones.end(), [](auto const& left, auto const& right) {
      return tie(left.sourcePriority, left.sequence) <
             tie(right.sourcePriority, right.sequence);
    });
    for (auto& clone : clones) {
      auto* raw = clone.primitive.get();
      mBuiltPrimitives.push_back(move(clone.primitive));
      context.appendPrimitive(
          raw, contentPhase(size), clone.sourcePriority);
    }
  };

  appendContent(PrefabTileSize::Size256);
  appendReplaceSquares(PrefabTileSize::Size128);
  appendContent(PrefabTileSize::Size128);
  appendReplaceSquares(PrefabTileSize::Size64);
  appendContent(PrefabTileSize::Size64);
  appendReplaceSquares(PrefabTileSize::Size32);
  appendContent(PrefabTileSize::Size32);
}

bool PrefabField::primitivesParticipateInBuild() const { return true; }
bool PrefabField::permitsDirectPrimitiveEditing() const { return false; }
bool PrefabField::acceptsNewPrimitives() const { return false; }
uint32_t PrefabField::adoptPrimitive(Primitive*) { throw CoreException("PrefabField does not accept Primitives"); }
void PrefabField::replacePrimitive(Primitive*, Primitive*) { throw CoreException("PrefabField output cannot be edited directly"); }
bool PrefabField::ownsPrimitive(Primitive const* primitive) const {
  return any_of(mBuiltPrimitives.begin(), mBuiltPrimitives.end(),
                [primitive](auto const& item) { return item.get() == primitive; });
}

void PrefabField::bind(Layer const& layer, DefinePrefabs const* step) {
  if (step) {
    auto* owned = layer.getStepById(step->getId());
    if (owned != step) throw CoreException("PrefabField cannot bind to a DefinePrefabs step on another Layer");
  }
  mDefinePrefabsStepId = step ? step->getId() : ~0u;
  mSelectedPrefabId = ~0u;
  mHasSelectedTile = false;
  modify();
}
uint32_t PrefabField::getDefinePrefabsStepId() const { return mDefinePrefabsStepId; }
DefinePrefabs* PrefabField::getDefinePrefabs(Layer const& layer) const {
  return dynamic_cast<DefinePrefabs*>(layer.getStepById(mDefinePrefabsStepId));
}
void PrefabField::setSelectedPrefab(DefinePrefabs const& definitions, Prefab const* prefab) {
  if (definitions.getId() != mDefinePrefabsStepId ||
      (prefab && definitions.findPrefabById(prefab->getId()) != prefab))
    throw CoreException("Cannot select a Prefab outside this PrefabField's binding");
  if (prefab && mHasSelectedTile &&
      mSelectedTile.size != prefab->getTileSize()) {
    mHasSelectedTile = false;
  }
  mSelectedPrefabId = prefab ? prefab->getId() : ~0u;
}
void PrefabField::clearSelectedPrefab() { mSelectedPrefabId = ~0u; }
Prefab* PrefabField::getSelectedPrefab(Layer const& layer) const {
  auto* definitions = getDefinePrefabs(layer);
  return definitions ? definitions->findPrefabById(mSelectedPrefabId) : nullptr;
}
void PrefabField::selectTile(Tile tile) {
  if (!isPrefabTileSize(prefabTileSide(tile.size))) {
    throw CoreException("Cannot select a Tile with an unknown size");
  }
  mSelectedTile = tile;
  mHasSelectedTile = true;
}
void PrefabField::clearSelectedTile() { mHasSelectedTile = false; }
bool PrefabField::hasSelectedTile() const { return mHasSelectedTile; }
Tile PrefabField::getSelectedTile() const { return mSelectedTile; }
Tile PrefabField::tileAt(PrefabTileSize size, wp::Vector2 const& position) const {
  auto const side = static_cast<float>(prefabTileSide(size));
  return {size, static_cast<int32_t>(floor(position.x / side)),
          static_cast<int32_t>(floor(position.y / side))};
}
Tile PrefabField::tileAt(Layer const& layer, wp::Vector2 const& position) const {
  auto* prefab = getSelectedPrefab(layer);
  if (!prefab) throw CoreException("Cannot address a Tile without a selected Prefab");
  return tileAt(prefab->getTileSize(), position);
}
bool PrefabField::selectOccupiedTileAt(wp::Vector2 const& position) {
  for (auto size : allPrefabTileSizes) {
    auto tile = tileAt(size, position);
    if (mInstances.contains(tile)) {
      selectTile(tile);
      return true;
    }
  }
  clearSelectedTile();
  return false;
}
bool PrefabField::placeSelected(Layer& layer, Tile tile) {
  auto* selected = getSelectedPrefab(layer);
  if (!selected || selected->getTileSize() != tile.size) return false;
  selectTile(tile);
  auto it = mInstances.find(tile);
  auto const mode = tile.size == PrefabTileSize::Size256
                        ? TileMode::Add
                        : it != mInstances.end() ? it->second.mode
                                                 : TileMode::Replace;
  PrefabInstance replacement{mSelectedPrefabId, 0, mode};
  if (it != mInstances.end() && it->second.prefabId == replacement.prefabId &&
      it->second.rotation == 0 && it->second.mode == replacement.mode)
    return false;
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
  it->second.rotation = next ? (current + 1) % count
                             : (current + count - 1) % count;
  modify();
  layer.rebuild();
  return true;
}
bool PrefabField::setInstanceMode(Layer& layer, Tile tile, TileMode mode) {
  auto it = mInstances.find(tile);
  if (it == mInstances.end() || tile.size == PrefabTileSize::Size256) return false;
  if (it->second.mode == mode) return false;
  it->second.mode = mode;
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
  return any_of(mInstances.begin(), mInstances.end(),
                [prefabId](auto const& item) { return item.second.prefabId == prefabId; });
}
bool PrefabField::canMigratePrefabSize(
    uint32_t prefabId, PrefabTileSize from, PrefabTileSize to) const {
  for (auto const& [tile, instance] : mInstances) {
    if (tile.size != from || instance.prefabId != prefabId) continue;
    if (mInstances.contains({to, tile.x, tile.y})) return false;
  }
  return true;
}
void PrefabField::migratePrefabSize(
    uint32_t prefabId, PrefabTileSize from, PrefabTileSize to) {
  if (from == to) return;
  if (!canMigratePrefabSize(prefabId, from, to)) {
    throw CoreException("Changing Prefab size would collide with an occupied destination Tile");
  }
  auto const selectedInstance =
      mHasSelectedTile ? getInstance(mSelectedTile) : nullptr;
  auto const migrateSelection =
      mHasSelectedTile && mSelectedTile.size == from &&
      (mSelectedPrefabId == prefabId ||
       (selectedInstance && selectedInstance->prefabId == prefabId));
  vector<pair<Tile, PrefabInstance>> moving;
  for (auto it = mInstances.begin(); it != mInstances.end();) {
    if (it->first.size == from && it->second.prefabId == prefabId) {
      auto instance = it->second;
      instance.mode = to == PrefabTileSize::Size256
                          ? TileMode::Add
                          : from == PrefabTileSize::Size256
                                ? TileMode::Replace
                                : instance.mode;
      moving.push_back({{to, it->first.x, it->first.y}, instance});
      it = mInstances.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& [tile, instance] : moving) mInstances.emplace(tile, instance);
  if (migrateSelection) mSelectedTile.size = to;
  if (!moving.empty()) modify();
}
bool PrefabField::isHiddenGeneratedPrimitive(Primitive const* primitive) const {
  return find(mHiddenPrimitives.begin(), mHiddenPrimitives.end(), primitive) !=
         mHiddenPrimitives.end();
}

void PrefabField::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeUint32("definePrefabsStepId", mDefinePrefabsStepId);
  serializer->beginArray("instances");
  for (auto const& [tile, instance] : mInstances) {
    serializer->beginMap("instance");
    serializer->writeUint32("tileSize", prefabTileSide(tile.size));
    serializer->writeInt32("x", tile.x);
    serializer->writeInt32("y", tile.y);
    serializer->writeUint32("prefabId", instance.prefabId);
    serializer->writeUint32("rotation", instance.rotation);
    serializer->writeUint32("mode", static_cast<uint32_t>(instance.mode));
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
    auto const sizeValue = serializer->readUint32("tileSize");
    if (!isPrefabTileSize(sizeValue)) throw CoreException("Unknown Tile size in PrefabField");
    Tile tile{static_cast<PrefabTileSize>(sizeValue), serializer->readInt32("x"),
              serializer->readInt32("y")};
    auto const prefabId = serializer->readUint32("prefabId");
    auto const rotation = serializer->readUint32("rotation");
    auto const modeValue = serializer->readUint32("mode");
    if (modeValue > static_cast<uint32_t>(TileMode::Add))
      throw CoreException("Unknown Tile mode in PrefabField");
    PrefabInstance instance{prefabId, rotation,
                            static_cast<TileMode>(modeValue)};
    if (tile.size == PrefabTileSize::Size256 && instance.mode != TileMode::Add)
      throw CoreException("A 256 Tile must use Add mode");
    if (!instances.emplace(tile, instance).second)
      throw CoreException("Duplicate Tile in PrefabField");
    serializer->endMap();
  }
  serializer->endArray();
  mDefinePrefabsStepId = stepId;
  mInstances = move(instances);
  mSelectedPrefabId = ~0u;
  mHasSelectedTile = false;
  mBuiltPrimitives.clear();
  mHiddenPrimitives.clear();
  return true;
}
}  // namespace bw::core
