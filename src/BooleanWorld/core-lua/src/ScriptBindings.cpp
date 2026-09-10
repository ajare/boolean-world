#include "core-lua/ScriptBindings.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/DefineTileMaps.h>
#include <core/Layer.h>
#include <core/MeshPrimitive.h>
#include <core/Primitive.h>
#include <core/PrimitiveField.h>
#include <core/TileMap.h>

#include "core-lua/RunScript.h"

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr char const* boundMarker = "__bw_script_types_bound";

bool integralNumberFromLua(
    sol::object const& value, lua_Number& integerValue) {
  auto* lua = value.lua_state();
  if (!lua) {
    return false;
  }
  value.push();
  auto const isNumber = lua_type(lua, -1) == LUA_TNUMBER;
  auto const number = isNumber ? lua_tonumber(lua, -1) : 0.0;
  lua_pop(lua, 1);
  if (!isNumber || !isfinite(number) || trunc(number) != number) {
    return false;
  }
  integerValue = number;
  return true;
}

int32_t tileCoordinateFromLua(sol::object const& value, char const* name) {
  lua_Number coordinate{};
  if (!integralNumberFromLua(value, coordinate)) {
    throw CoreException(format("Prefab Tile {} must be an integer", name));
  }
  if (coordinate < numeric_limits<int32_t>::min() ||
      coordinate > numeric_limits<int32_t>::max()) {
    throw CoreException(format("Prefab Tile {} is out of range", name));
  }
  return static_cast<int32_t>(coordinate);
}

uint32_t prefabGridSizeFromLua(sol::object const& value) {
  lua_Number size{};
  if (!integralNumberFromLua(value, size)) {
    throw CoreException("Prefab grid size must be an integer");
  }
  if (size < 0 || size > numeric_limits<uint32_t>::max() ||
      !isPrefabTileSize(static_cast<uint32_t>(size))) {
    throw CoreException("Prefab grid size must be 32, 64, 128, or 256");
  }
  return static_cast<uint32_t>(size);
}

string operationName(Primitive::Operation operation) {
  switch (operation) {
    case Primitive::Operation::Union: return "union";
    case Primitive::Operation::Intersection: return "intersection";
    case Primitive::Operation::Difference: return "difference";
    case Primitive::Operation::XOR: return "xor";
  }
  throw CoreException("Unknown Primitive operation");
}

Primitive::Operation operationFromName(string const& name) {
  if (name == "union") return Primitive::Operation::Union;
  if (name == "intersection") return Primitive::Operation::Intersection;
  if (name == "difference") return Primitive::Operation::Difference;
  if (name == "xor") return Primitive::Operation::XOR;
  throw CoreException(format("'{}' is not a Primitive operation", name));
}

void setElevation(
    Primitive& primitive, bool floor, float angle, float lower,
    float upper) {
  if (!isfinite(angle) || !isfinite(lower) || !isfinite(upper)) {
    throw CoreException("Elevation-span values must be finite");
  }
  auto properties = primitive.getProperties();
  auto& span = floor ? properties.floorSpan : properties.ceilingSpan;
  span = ElevationSpan{angle, lower, upper};
  primitive.setProperties(properties);
}

tuple<float, float, float> getElevation(
    Primitive const& primitive, bool floor) {
  auto const& properties = primitive.getProperties();
  auto const& span = floor ? properties.floorSpan : properties.ceilingSpan;
  return {span.directionAngle, span.lowerElevation, span.upperElevation};
}

enum class MaterialSurface { Floor, Ceiling, Wall };

void setMaterial(
    Primitive& primitive, MaterialSurface surface, string const& materialId) {
  auto properties = primitive.getProperties();
  auto material = SurfaceMaterialReference::subMaterial(materialId);
  switch (surface) {
    case MaterialSurface::Floor: properties.floorMaterialId = material; break;
    case MaterialSurface::Ceiling: properties.ceilingMaterialId = material; break;
    case MaterialSurface::Wall: properties.wallMaterialId = material; break;
  }
  primitive.setProperties(properties);
}

SurfaceMaterialReference const& getSurfaceMaterial(
    Primitive const& primitive, MaterialSurface surface) {
  auto const& properties = primitive.getProperties();
  switch (surface) {
    case MaterialSurface::Floor: return properties.floorMaterialId;
    case MaterialSurface::Ceiling: return properties.ceilingMaterialId;
    case MaterialSurface::Wall: return properties.wallMaterialId;
  }
  throw CoreException("Unknown Primitive surface");
}

string const& getMaterial(
    Primitive const& primitive, MaterialSurface surface) {
  return getSurfaceMaterial(primitive, surface).reference;
}

void setSurfaceMaterial(
    Primitive& primitive, MaterialSurface surface, string const& kind,
    string const& reference) {
  auto materialKind = surfaceMaterialKindFromName(kind);
  if (reference.empty()) {
    throw CoreException("Surface material reference must not be empty");
  }
  if (materialKind == SurfaceMaterialKind::Triplanar &&
      reference.find('/') == string::npos) {
    throw CoreException(
        "Triplanar Surface material reference must be qualified");
  }

  auto properties = primitive.getProperties();
  auto material = SurfaceMaterialReference{materialKind, reference};
  switch (surface) {
    case MaterialSurface::Floor: properties.floorMaterialId = material; break;
    case MaterialSurface::Ceiling: properties.ceilingMaterialId = material; break;
    case MaterialSurface::Wall: properties.wallMaterialId = material; break;
  }
  primitive.setProperties(properties);
}

tuple<string, string> getSurfaceMaterialValues(
    Primitive const& primitive, MaterialSurface surface) {
  auto const& material = getSurfaceMaterial(primitive, surface);
  return {string(surfaceMaterialKindName(material.kind)), material.reference};
}

set<string> prefabTagsFromTable(sol::table const& values) {
  auto const size = values.size();
  vector<bool> present(size, false);
  set<string> tags;
  size_t entries = 0;

  for (auto const& keyValue : values) {
    ++entries;
    auto const& key = keyValue.first;
    auto const& value = keyValue.second;
    if (!key.is<lua_Integer>()) {
      throw CoreException("Prefab tags must be a dense array of strings");
    }
    auto const index = key.as<lua_Integer>();
    if (index < 1 || static_cast<size_t>(index) > size ||
        present[static_cast<size_t>(index) - 1]) {
      throw CoreException("Prefab tags must be a dense array of strings");
    }
    if (!value.is<string>()) {
      throw CoreException(format("Prefab tag {} must be a string", index));
    }
    present[static_cast<size_t>(index) - 1] = true;
    tags.insert(value.as<string>());
  }

  if (entries != size) {
    throw CoreException("Prefab tags must be a dense array of strings");
  }
  return tags;
}

vector<PrefabView> toPrefabViews(vector<Prefab*> const& prefabs) {
  vector<PrefabView> views;
  views.reserve(prefabs.size());
  for (auto const* prefab : prefabs) {
    views.push_back(PrefabView{prefab});
  }
  return views;
}

map<string, string> metadataFromTable(
    sol::table const& values, string const& subject) {
  map<string, string> metadata;
  for (auto const& keyValue : values) {
    auto const& key = keyValue.first;
    auto const& value = keyValue.second;
    if (!key.is<string>() || !value.is<string>()) {
      throw CoreException(format(
          "{} metadata filters must contain only string keys and values",
          subject));
    }
    auto name = key.as<string>();
    if (name.empty()) {
      throw CoreException(format("{} metadata keys cannot be empty", subject));
    }
    metadata.emplace(move(name), value.as<string>());
  }
  return metadata;
}

bool metadataMatches(
    map<string, string> const& metadata,
    map<string, string> const& required) {
  return all_of(
      required.begin(), required.end(), [&](auto const& entry) {
        auto found = metadata.find(entry.first);
        return found != metadata.end() && found->second == entry.second;
      });
}

vector<PrefabVertexView> prefabMetadataVertices(
    Prefab const& prefab, map<string, string> const& required) {
  vector<PrefabVertexView> result;
  for (auto const* primitive : prefab.getPrimitives()) {
    set<pair<float, float>> seen;
    for (auto const& polygon : primitive->getVertices()) {
      for (auto const& ring : polygon) {
        for (auto const& vertex : ring) {
          if (vertex.metadata.empty() ||
              !seen.emplace(vertex.p.x, vertex.p.y).second) {
            continue;
          }
          if (metadataMatches(vertex.metadata, required)) {
            result.push_back(
                PrefabVertexView{vertex.p.x, vertex.p.y, vertex.metadata});
          }
        }
      }
    }
  }
  return result;
}

vector<PrefabEdgeView> prefabMetadataEdges(
    Prefab const& prefab, map<string, string> const& required) {
  vector<PrefabEdgeView> result;
  using Point = pair<float, float>;
  using Edge = pair<Point, Point>;
  for (auto const* primitive : prefab.getPrimitives()) {
    set<Edge> seen;
    for (auto const& polygon : primitive->getVertices()) {
      for (auto const& ring : polygon) {
        for (size_t index = 0; index < ring.size(); ++index) {
          auto const& first = ring[index];
          auto const& second = ring[(index + 1) % ring.size()];
          Point firstPoint{first.p.x, first.p.y};
          Point secondPoint{second.p.x, second.p.y};
          auto key = minmax(firstPoint, secondPoint);
          if (first.edgeMetadata.empty() ||
              !seen.emplace(key.first, key.second).second) {
            continue;
          }
          if (metadataMatches(first.edgeMetadata, required)) {
            result.push_back(PrefabEdgeView{
                first.p.x, first.p.y, second.p.x, second.p.y,
                first.edgeMetadata});
          }
        }
      }
    }
  }
  return result;
}

RunScriptContext executionContext(sol::this_environment current) {
  if (!current) {
    throw CoreException("AudioEmitters may only be created during a RunScript execution");
  }
  sol::environment& environment = current;
  sol::object context = environment["context"];
  if (!context.is<RunScriptContext>()) {
    throw CoreException("AudioEmitters may only be created during a RunScript execution");
  }
  return context.as<RunScriptContext>();
}

ClosedPolygon ringFromPoints(sol::table const& points) {
  ClosedPolygon ring;
  ring.reserve(points.size());
  for (size_t index = 1; index <= points.size(); ++index) {
    sol::object value = points[index];
    if (!value.is<sol::table>()) {
      throw CoreException(format(
          "MeshPrimitive point {} must be a {{x, y}} table", index));
    }
    auto point = value.as<sol::table>();
    sol::object x = point[1];
    sol::object y = point[2];
    if (!x.is<float>() || !y.is<float>()) {
      throw CoreException(format(
          "MeshPrimitive point {} must contain numeric x and y values", index));
    }
    ring.emplace_back(wp::Vector2{x.as<float>(), y.as<float>()});
  }
  return ring;
}

}  // namespace

vector<PrimitiveView> toPrimitiveViews(vector<Primitive*> const& primitives) {
  vector<PrimitiveView> views;
  views.reserve(primitives.size());
  for (auto* primitive : primitives) {
    views.push_back(PrimitiveView{primitive});
  }
  return views;
}

ScriptAudioEmitter::ScriptAudioEmitter(Primitive* primitive, string guid)
    : mPrimitive(primitive), mGuid(move(guid)) {
}

namespace {

AudioEmitter const& findAudioEmitter(ScriptAudioEmitter const& handle) {
  auto const& emitters = handle.getPrimitive()->getAudioEmitters();
  auto found = find_if(emitters.begin(), emitters.end(), [&handle](auto const& emitter) {
    return emitter.guid == handle.getGuid();
  });
  if (found == emitters.end()) {
    throw CoreException("AudioEmitter is no longer owned by its Primitive");
  }
  return *found;
}

template <typename Change>
void changeAudioEmitter(ScriptAudioEmitter const& handle, Change change) {
  auto emitters = handle.getPrimitive()->getAudioEmitters();
  auto found = find_if(emitters.begin(), emitters.end(), [&handle](auto const& emitter) {
    return emitter.guid == handle.getGuid();
  });
  if (found == emitters.end()) {
    throw CoreException("AudioEmitter is no longer owned by its Primitive");
  }
  change(*found);
  handle.getPrimitive()->setAudioEmitters(emitters);
}

}  // namespace

tuple<float, float> ScriptAudioEmitter::getOffset() const {
  auto const& offset = findAudioEmitter(*this).offset;
  return {offset.x, offset.y};
}

void ScriptAudioEmitter::setOffset(float x, float y) const {
  changeAudioEmitter(*this, [x, y](auto& emitter) {
    emitter.offset = {x, y};
  });
}

float ScriptAudioEmitter::getHeightOffset() const {
  return findAudioEmitter(*this).heightOffset;
}

void ScriptAudioEmitter::setHeightOffset(float height) const {
  changeAudioEmitter(*this, [height](auto& emitter) {
    emitter.heightOffset = height;
  });
}

string ScriptAudioEmitter::getSoundId() const {
  return findAudioEmitter(*this).soundId;
}

void ScriptAudioEmitter::setSoundId(string const& soundId) const {
  changeAudioEmitter(*this, [&soundId](auto& emitter) {
    emitter.soundId = soundId;
  });
}

Primitive* ScriptAudioEmitter::getPrimitive() const {
  return mPrimitive;
}

string const& ScriptAudioEmitter::getGuid() const {
  return mGuid;
}

RunScriptContext::RunScriptContext(
    RunScript const& step, LayerBuildContext& build)
    : mStep(&step), mBuild(&build) {
}

Primitive* RunScriptContext::createPrimitive(string const& type) const {
  return mStep->createPrimitive(type);
}

ScriptAudioEmitter RunScriptContext::addAudioEmitter(
    Primitive* primitive, float x, float y, float heightOffset,
    string const& soundId) const {
  if (!primitive || !mStep->ownsPrimitive(primitive)) {
    throw CoreException(
        "A script added an AudioEmitter to a Primitive its RunScript step does not own");
  }

  AudioEmitter emitter;
  emitter.offset = {x, y};
  emitter.heightOffset = heightOffset;
  emitter.soundId = soundId;
  emitter.guid = mStep->nextAudioEmitterGuid();
  auto emitters = primitive->getAudioEmitters();
  emitters.push_back(emitter);
  primitive->setAudioEmitters(emitters);
  return ScriptAudioEmitter(primitive, move(emitter.guid));
}

vector<ScriptAudioEmitter> RunScriptContext::getAudioEmitters(
    Primitive* primitive) const {
  vector<ScriptAudioEmitter> result;
  if (!primitive) return result;
  result.reserve(primitive->getAudioEmitters().size());
  for (auto const& emitter : primitive->getAudioEmitters()) {
    result.emplace_back(primitive, emitter.guid);
  }
  return result;
}

bool RunScriptContext::removeAudioEmitter(
    Primitive* primitive, ScriptAudioEmitter const& emitter) const {
  if (!primitive || emitter.getPrimitive() != primitive) return false;
  auto emitters = primitive->getAudioEmitters();
  auto found = find_if(emitters.begin(), emitters.end(), [&emitter](auto const& candidate) {
    return candidate.guid == emitter.getGuid();
  });
  if (found == emitters.end()) return false;
  emitters.erase(found);
  primitive->setAudioEmitters(emitters);
  return true;
}

ScriptMeshPrimitive::ScriptMeshPrimitive(MeshPrimitive* primitive)
    : mPrimitive(primitive), mEditing(primitive->createEditingProxy()) {
}

MeshPrimitive* ScriptMeshPrimitive::getPrimitive() const {
  return mPrimitive;
}

void ScriptMeshPrimitive::primitiveTransformChanged() {
  auto refreshed = mPrimitive->createEditingProxy();
  auto oldMappings = mEditing->getNodeMappings();
  auto newMappings = refreshed->getNodeMappings();
  if (oldMappings.size() != newMappings.size()) {
    mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
    return;
  }

  auto candidate = mEditing->getMesh();
  for (size_t index = 0; index < oldMappings.size(); ++index) {
    auto oldVertices = mEditing->getPolygon(oldMappings[index].polygonIndex)
                           .getOrderedVertexIndices();
    auto newVertices = refreshed->getPolygon(newMappings[index].polygonIndex)
                           .getOrderedVertexIndices();
    if (oldVertices.size() != newVertices.size()) {
      mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
      return;
    }
    for (size_t vertex = 0; vertex < oldVertices.size(); ++vertex) {
      candidate.moveVertexTo(
          oldVertices[vertex],
          refreshed->getVertex(newVertices[vertex]).getPosition());
    }
  }

  if (!mEditing->replaceMesh(move(candidate))) {
    mEditing = shared_ptr<MeshPrimitiveEditingProxy>(move(refreshed));
  }
}

bool ScriptMeshPrimitive::moveVertexTo(
    uint32_t vertexId, float x, float y) {
  if (mEditing->vertexIndexIterationFinished(vertexId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveVertexTo(vertexId, {x, y});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::moveVertex(
    uint32_t vertexId, float deltaX, float deltaY) {
  if (mEditing->vertexIndexIterationFinished(vertexId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveVertex(vertexId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::moveEdge(
    uint32_t edgeId, float deltaX, float deltaY) {
  if (mEditing->edgeIndexIterationFinished(edgeId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.moveEdge(edgeId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::movePolygon(
    uint32_t polygonId, float deltaX, float deltaY) {
  if (mEditing->polygonIndexIterationFinished(polygonId)) return false;
  auto candidate = mEditing->getMesh();
  candidate.movePolygon(polygonId, {deltaX, deltaY});
  if (!mEditing->replaceMesh(move(candidate))) return false;
  mEditing->commitTo(*mPrimitive);
  return true;
}

optional<uint32_t> ScriptMeshPrimitive::splitEdge(uint32_t edgeId) {
  return splitEdge(edgeId, 0.5f);
}

optional<uint32_t> ScriptMeshPrimitive::splitEdge(
    uint32_t edgeId, float t) {
  if (mEditing->edgeIndexIterationFinished(edgeId) || t <= 0.0f || t >= 1.0f) {
    return nullopt;
  }
  wp::geometry::SplitEdgeResult result;
  if (!mEditing->splitEdge(edgeId, t, &result) ||
      result.newVertexIndices.empty()) {
    return nullopt;
  }
  mEditing->commitTo(*mPrimitive);
  return result.newVertexIndices.front();
}

bool ScriptMeshPrimitive::removeVertex(uint32_t vertexId) {
  if (mEditing->vertexIndexIterationFinished(vertexId) ||
      !mEditing->removeVertex(vertexId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::removeEdge(uint32_t edgeId) {
  if (mEditing->edgeIndexIterationFinished(edgeId) ||
      !mEditing->removeEdge(edgeId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

bool ScriptMeshPrimitive::removePolygon(uint32_t polygonId) {
  if (mEditing->polygonIndexIterationFinished(polygonId) ||
      !mEditing->removeRing(polygonId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

optional<uint32_t> ScriptMeshPrimitive::addShell(sol::table const& points) {
  auto id = mEditing->addShell(ringFromPoints(points));
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::addHole(
    uint32_t filledPolygonId, sol::table const& points) {
  auto id = mEditing->addHole(filledPolygonId, ringFromPoints(points));
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::addIsland(
    uint32_t holePolygonId, sol::table const& points) {
  auto id = mEditing->addIsland(holePolygonId, ringFromPoints(points));
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

optional<uint32_t> ScriptMeshPrimitive::fillHole(uint32_t holePolygonId) {
  auto id = mEditing->fillHole(holePolygonId);
  if (id == ~0u) return nullopt;
  mEditing->commitTo(*mPrimitive);
  return id;
}

bool ScriptMeshPrimitive::slicePolygon(
    uint32_t polygonId, uint32_t firstVertexId,
    uint32_t secondVertexId) {
  if (!mEditing->sliceFilledRing(
          polygonId, firstVertexId, secondVertexId)) {
    return false;
  }
  mEditing->commitTo(*mPrimitive);
  return true;
}

ScriptMeshPrimitive RunScriptContext::createMeshPrimitive(sol::table const& points) const {
  auto ring = ringFromPoints(points);
  auto primitive = unique_ptr<MeshPrimitive>(MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union, {{move(ring)}}));
  auto properties = primitive->getProperties();
  properties.floorMaterialId =
      SurfaceMaterialReference::subMaterial("builtin.plain.grey");
  properties.ceilingMaterialId = properties.floorMaterialId;
  properties.wallMaterialId = properties.floorMaterialId;
  primitive->setProperties(properties);
  auto* borrowed = primitive.get();
  (void)mStep->ownPrimitive(move(primitive));
  return ScriptMeshPrimitive(borrowed);
}

void RunScriptContext::placePrimitive(Primitive* primitive) const {
  mStep->placePrimitive(*mBuild, primitive);
}

void RunScriptContext::placeMeshPrimitive(
    ScriptMeshPrimitive const& primitive) const {
  placePrimitive(primitive.getPrimitive());
}

void RunScriptContext::placePrefabInstance(
    PrefabView view, int32_t tileX, int32_t tileY, float angle) const {
  mStep->placePrefabInstance(*mBuild, view.prefab, tileX, tileY, angle);
}

tuple<int32_t, int32_t> RunScriptContext::getTile(
    uint32_t gridSize, float x, float y) const {
  if (!isPrefabTileSize(gridSize)) {
    throw CoreException("Prefab grid size must be 32, 64, 128, or 256");
  }
  if (!isfinite(x) || !isfinite(y)) {
    throw CoreException("A World position used to find a Tile must be finite");
  }

  auto coordinate = [gridSize](float value) {
    auto const result = floor(static_cast<double>(value) / gridSize);
    if (result < numeric_limits<int32_t>::min() ||
        result > numeric_limits<int32_t>::max()) {
      throw CoreException("Prefab Tile coordinate is out of range");
    }
    return static_cast<int32_t>(result);
  };
  return {coordinate(x), coordinate(y)};
}

DefinePrefabsView RunScriptContext::findDefinePrefabs(string const& name) const {
  auto const id = mBuild->getLayer().findStepIdByName(name);
  if (id == ~0u) {
    throw CoreException(format("No step named '{}'", name));
  }
  auto* step = dynamic_cast<DefinePrefabs*>(mBuild->getLayer().getStepById(id));
  if (!step) {
    throw CoreException(format("Step '{}' is not a DefinePrefabs step", name));
  }
  return DefinePrefabsView{step};
}

PrimitiveFieldView RunScriptContext::findPrimitiveField(string const& name) const {
  auto const id = mBuild->getLayer().findStepIdByName(name);
  if (id == ~0u) {
    throw CoreException(format("No step named '{}'", name));
  }
  auto* step = dynamic_cast<PrimitiveField*>(mBuild->getLayer().getStepById(id));
  if (!step) {
    throw CoreException(format("Step '{}' is not a PrimitiveField step", name));
  }
  return PrimitiveFieldView{step};
}

TileMapView RunScriptContext::findTileMap(
    string const& name, uint32_t index) const {
  auto& layer = mBuild->getLayer();
  auto const id = layer.findStepIdByName(name);
  if (id == ~0u) {
    throw CoreException(format("No step named '{}'", name));
  }
  auto* definitions = dynamic_cast<DefineTileMaps*>(layer.getStepById(id));
  if (!definitions) {
    throw CoreException(format("Step '{}' is not a DefineTileMaps step", name));
  }

  uint32_t definitionsIndex = ~0u;
  uint32_t runScriptIndex = ~0u;
  for (uint32_t stepIndex = 0; stepIndex < layer.getNumSteps(); ++stepIndex) {
    auto* candidate = layer.getStep(stepIndex);
    if (candidate == definitions) definitionsIndex = stepIndex;
    if (candidate == mStep) runScriptIndex = stepIndex;
  }
  if (!definitions->isEnabled()) {
    throw CoreException(format("DefineTileMaps step '{}' is disabled", name));
  }
  if (definitionsIndex == ~0u || runScriptIndex == ~0u ||
      definitionsIndex >= runScriptIndex) {
    throw CoreException(format(
        "DefineTileMaps step '{}' must precede this RunScript step", name));
  }
  return TileMapView{definitions->getTileMap(index)};
}

vector<PrimitiveView> RunScriptContext::getBuildPrimitives() const {
  return toPrimitiveViews(mBuild->getBuildPrimitives());
}

tuple<float, float, float, float> RunScriptContext::getExtents() const {
  auto const& extents = mBuild->getExtents();
  return make_tuple(
      extents.getPosition().x, extents.getPosition().y,
      extents.getSize().x, extents.getSize().y);
}

vector<PrimitiveView> RunScriptContext::findBuildPrimitivesOverlapping(
    float x, float y, float width, float height) const {
  auto primitives = mBuild->findBuildPrimitivesOverlapping(
      wp::BoundingBox(x, y, width, height));
  erase_if(primitives, [](auto const* primitive) {
    return primitive->hasFlag(BW_PRIMITIVE_GHOST_FLAG);
  });
  return toPrimitiveViews(primitives);
}

void bindScriptTypes(sol::state& lua) {
  if (lua[boundMarker].valid()) {
    return;
  }

  lua.new_usertype<ScriptAudioEmitter>(
      "AudioEmitter", sol::no_constructor,
      "get_offset", &ScriptAudioEmitter::getOffset,
      "set_offset", &ScriptAudioEmitter::setOffset,
      "get_height_offset", &ScriptAudioEmitter::getHeightOffset,
      "set_height_offset", &ScriptAudioEmitter::setHeightOffset,
      "get_sound_id", &ScriptAudioEmitter::getSoundId,
      "set_sound_id", &ScriptAudioEmitter::setSoundId);

  lua.new_usertype<Primitive>(
      "Primitive", sol::no_constructor,

      "add_audio_emitter",
      sol::overload(
          [](Primitive& primitive, sol::this_environment environment) {
            return executionContext(environment).addAudioEmitter(&primitive, 0.0f, 0.0f, 0.0f, "");
          },
          [](Primitive& primitive, float x, float y, float heightOffset,
             string const& soundId, sol::this_environment environment) {
            return executionContext(environment).addAudioEmitter(&primitive, x, y, heightOffset, soundId);
          }),
      "get_audio_emitters",
      [](Primitive& primitive, sol::this_environment environment) {
        return sol::as_table(
            executionContext(environment).getAudioEmitters(&primitive));
      },
      "remove_audio_emitter",
      [](Primitive& primitive, ScriptAudioEmitter const& emitter,
         sol::this_environment environment) {
        return executionContext(environment).removeAudioEmitter(&primitive, emitter);
      },

      "get_type", &Primitive::getType,

      "set_position",
      [](Primitive& primitive, float x, float y) {
        primitive.setPosition({x, y});
      },
      "get_position",
      [](Primitive const& primitive) {
        return make_tuple(primitive.getPosition().x, primitive.getPosition().y);
      },

      "set_transform_offset",
      [](Primitive& primitive, float x, float y) {
        primitive.setTransformOffset({x, y});
      },
      "get_transform_offset",
      [](Primitive const& primitive) {
        return make_tuple(
            primitive.getTransformOffset().x,
            primitive.getTransformOffset().y);
      },

      "set_orientation", &Primitive::setOrientation,
      "get_orientation", &Primitive::getOrientation,

      "set_follow_orbit_angle", &Primitive::setFollowOrbitAngle,
      "get_follow_orbit_angle", &Primitive::getFollowOrbitAngle,

      "set_influence_eye_origin_offset",
      [](Primitive& primitive, float x, float y) {
        primitive.setInfluenceEyeOriginOffset({x, y});
      },
      "get_influence_eye_origin_offset",
      [](Primitive const& primitive) {
        return make_tuple(
            primitive.getInfluenceEyeOriginOffset().x,
            primitive.getInfluenceEyeOriginOffset().y);
      },
      "get_influence_eye_origin_position",
      [](Primitive const& primitive) {
        auto const position = primitive.getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "set_influence_eye_angle_offset", &Primitive::setInfluenceEyeAngleOffset,
      "get_influence_eye_angle_offset", &Primitive::getInfluenceEyeAngleOffset,

      "is_static", &Primitive::isStatic,

      "set_size",
      [](Primitive& primitive, float x, float y) { primitive.setSize(x, y); },
      "get_size",
      [](Primitive const& primitive) {
        return make_tuple(primitive.getSize().x, primitive.getSize().y);
      },

      "set_priority",
      [](Primitive& primitive, uint32_t priority) {
        primitive.setPriority(static_cast<uint8_t>(priority));
      },
      "get_priority",
      [](Primitive const& primitive) {
        return static_cast<uint32_t>(primitive.getPriority());
      },

      "set_operation",
      [](Primitive& primitive, string const& operation) {
        primitive.setOperation(operationFromName(operation));
      },
      "get_operation",
      [](Primitive const& primitive) {
        return operationName(primitive.getOperation());
      },

      "set_floor_elevation",
      [](Primitive& primitive, float angle, float lower, float upper) {
        setElevation(primitive, true, angle, lower, upper);
      },
      "get_floor_elevation",
      [](Primitive const& primitive) { return getElevation(primitive, true); },
      "set_ceiling_elevation",
      [](Primitive& primitive, float angle, float lower, float upper) {
        setElevation(primitive, false, angle, lower, upper);
      },
      "get_ceiling_elevation",
      [](Primitive const& primitive) { return getElevation(primitive, false); },

      "set_floor_material",
      [](Primitive& primitive, string const& materialId) {
        setMaterial(primitive, MaterialSurface::Floor, materialId);
      },
      "get_floor_material",
      [](Primitive const& primitive) {
        return getMaterial(primitive, MaterialSurface::Floor);
      },
      "set_ceiling_material",
      [](Primitive& primitive, string const& materialId) {
        setMaterial(primitive, MaterialSurface::Ceiling, materialId);
      },
      "get_ceiling_material",
      [](Primitive const& primitive) {
        return getMaterial(primitive, MaterialSurface::Ceiling);
      },
      "set_wall_material",
      [](Primitive& primitive, string const& materialId) {
        setMaterial(primitive, MaterialSurface::Wall, materialId);
      },
      "get_wall_material",
      [](Primitive const& primitive) {
        return getMaterial(primitive, MaterialSurface::Wall);
      },
      "set_floor_surface_material",
      [](Primitive& primitive, string const& kind, string const& reference) {
        setSurfaceMaterial(primitive, MaterialSurface::Floor, kind, reference);
      },
      "get_floor_surface_material",
      [](Primitive const& primitive) {
        return getSurfaceMaterialValues(primitive, MaterialSurface::Floor);
      },
      "set_ceiling_surface_material",
      [](Primitive& primitive, string const& kind, string const& reference) {
        setSurfaceMaterial(primitive, MaterialSurface::Ceiling, kind, reference);
      },
      "get_ceiling_surface_material",
      [](Primitive const& primitive) {
        return getSurfaceMaterialValues(primitive, MaterialSurface::Ceiling);
      },
      "set_wall_surface_material",
      [](Primitive& primitive, string const& kind, string const& reference) {
        setSurfaceMaterial(primitive, MaterialSurface::Wall, kind, reference);
      },
      "get_wall_surface_material",
      [](Primitive const& primitive) {
        return getSurfaceMaterialValues(primitive, MaterialSurface::Wall);
      });

  lua.new_usertype<ScriptMeshPrimitive>(
      "MeshPrimitive", sol::no_constructor,

      "get_type",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getType();
      },

      "add_audio_emitter",
      sol::overload(
          [](ScriptMeshPrimitive& mesh, sol::this_environment environment) {
            return executionContext(environment).addAudioEmitter(mesh.getPrimitive(), 0.0f, 0.0f, 0.0f, "");
          },
          [](ScriptMeshPrimitive& mesh, float x, float y, float heightOffset,
             string const& soundId, sol::this_environment environment) {
            return executionContext(environment).addAudioEmitter(mesh.getPrimitive(), x, y, heightOffset, soundId);
          }),
      "get_audio_emitters",
      [](ScriptMeshPrimitive& mesh, sol::this_environment environment) {
        return sol::as_table(executionContext(environment).getAudioEmitters(mesh.getPrimitive()));
      },
      "remove_audio_emitter",
      [](ScriptMeshPrimitive& mesh, ScriptAudioEmitter const& emitter,
         sol::this_environment environment) {
        return executionContext(environment).removeAudioEmitter(mesh.getPrimitive(), emitter);
      },

      "set_position",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setPosition({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_position",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& position = mesh.getPrimitive()->getPosition();
        return make_tuple(position.x, position.y);
      },

      "set_transform_offset",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setTransformOffset({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_transform_offset",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& offset = mesh.getPrimitive()->getTransformOffset();
        return make_tuple(offset.x, offset.y);
      },

      "set_orientation",
      [](ScriptMeshPrimitive& mesh, float angle) {
        mesh.getPrimitive()->setOrientation(angle);
        mesh.primitiveTransformChanged();
      },
      "get_orientation",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getOrientation();
      },

      "set_follow_orbit_angle",
      [](ScriptMeshPrimitive& mesh, bool follow) {
        mesh.getPrimitive()->setFollowOrbitAngle(follow);
        mesh.primitiveTransformChanged();
      },
      "get_follow_orbit_angle",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getFollowOrbitAngle();
      },

      "set_influence_eye_origin_offset",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setInfluenceEyeOriginOffset({x, y});
        mesh.primitiveTransformChanged();
      },
      "get_influence_eye_origin_offset",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& offset = mesh.getPrimitive()->getInfluenceEyeOriginOffset();
        return make_tuple(offset.x, offset.y);
      },
      "get_influence_eye_origin_position",
      [](ScriptMeshPrimitive const& mesh) {
        auto const position = mesh.getPrimitive()->getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "set_influence_eye_angle_offset",
      [](ScriptMeshPrimitive& mesh, float angle) {
        mesh.getPrimitive()->setInfluenceEyeAngleOffset(angle);
        mesh.primitiveTransformChanged();
      },
      "get_influence_eye_angle_offset",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->getInfluenceEyeAngleOffset();
      },

      "is_static",
      [](ScriptMeshPrimitive const& mesh) {
        return mesh.getPrimitive()->isStatic();
      },

      "set_size",
      [](ScriptMeshPrimitive& mesh, float x, float y) {
        mesh.getPrimitive()->setSize(x, y);
        mesh.primitiveTransformChanged();
      },
      "get_size",
      [](ScriptMeshPrimitive const& mesh) {
        auto const& size = mesh.getPrimitive()->getSize();
        return make_tuple(size.x, size.y);
      },

      "set_priority",
      [](ScriptMeshPrimitive& mesh, uint32_t priority) {
        mesh.getPrimitive()->setPriority(static_cast<uint8_t>(priority));
      },
      "get_priority",
      [](ScriptMeshPrimitive const& mesh) {
        return static_cast<uint32_t>(mesh.getPrimitive()->getPriority());
      },

      "set_operation",
      [](ScriptMeshPrimitive& mesh, string const& operation) {
        mesh.getPrimitive()->setOperation(operationFromName(operation));
      },
      "get_operation",
      [](ScriptMeshPrimitive const& mesh) {
        return operationName(mesh.getPrimitive()->getOperation());
      },

      "set_floor_elevation",
      [](ScriptMeshPrimitive& mesh, float angle, float lower,
         float upper) {
        setElevation(*mesh.getPrimitive(), true, angle, lower, upper);
      },
      "get_floor_elevation",
      [](ScriptMeshPrimitive const& mesh) {
        return getElevation(*mesh.getPrimitive(), true);
      },
      "set_ceiling_elevation",
      [](ScriptMeshPrimitive& mesh, float angle, float lower,
         float upper) {
        setElevation(*mesh.getPrimitive(), false, angle, lower, upper);
      },
      "get_ceiling_elevation",
      [](ScriptMeshPrimitive const& mesh) {
        return getElevation(*mesh.getPrimitive(), false);
      },

      "set_floor_material",
      [](ScriptMeshPrimitive& mesh, string const& materialId) {
        setMaterial(*mesh.getPrimitive(), MaterialSurface::Floor, materialId);
      },
      "get_floor_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getMaterial(*mesh.getPrimitive(), MaterialSurface::Floor);
      },
      "set_ceiling_material",
      [](ScriptMeshPrimitive& mesh, string const& materialId) {
        setMaterial(*mesh.getPrimitive(), MaterialSurface::Ceiling, materialId);
      },
      "get_ceiling_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getMaterial(*mesh.getPrimitive(), MaterialSurface::Ceiling);
      },
      "set_wall_material",
      [](ScriptMeshPrimitive& mesh, string const& materialId) {
        setMaterial(*mesh.getPrimitive(), MaterialSurface::Wall, materialId);
      },
      "get_wall_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getMaterial(*mesh.getPrimitive(), MaterialSurface::Wall);
      },
      "set_floor_surface_material",
      [](ScriptMeshPrimitive& mesh, string const& kind,
         string const& reference) {
        setSurfaceMaterial(
            *mesh.getPrimitive(), MaterialSurface::Floor, kind, reference);
      },
      "get_floor_surface_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getSurfaceMaterialValues(
            *mesh.getPrimitive(), MaterialSurface::Floor);
      },
      "set_ceiling_surface_material",
      [](ScriptMeshPrimitive& mesh, string const& kind,
         string const& reference) {
        setSurfaceMaterial(
            *mesh.getPrimitive(), MaterialSurface::Ceiling, kind, reference);
      },
      "get_ceiling_surface_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getSurfaceMaterialValues(
            *mesh.getPrimitive(), MaterialSurface::Ceiling);
      },
      "set_wall_surface_material",
      [](ScriptMeshPrimitive& mesh, string const& kind,
         string const& reference) {
        setSurfaceMaterial(
            *mesh.getPrimitive(), MaterialSurface::Wall, kind, reference);
      },
      "get_wall_surface_material",
      [](ScriptMeshPrimitive const& mesh) {
        return getSurfaceMaterialValues(
            *mesh.getPrimitive(), MaterialSurface::Wall);
      },

      "split_edge",
      sol::overload(
          [](ScriptMeshPrimitive& mesh, uint32_t edgeId) {
            return mesh.splitEdge(edgeId);
          },
          [](ScriptMeshPrimitive& mesh, uint32_t edgeId, float t) {
            return mesh.splitEdge(edgeId, t);
          }),
      "move_vertex_to", &ScriptMeshPrimitive::moveVertexTo,
      "move_vertex", &ScriptMeshPrimitive::moveVertex,
      "move_edge", &ScriptMeshPrimitive::moveEdge,
      "move_polygon", &ScriptMeshPrimitive::movePolygon,
      "remove_vertex", &ScriptMeshPrimitive::removeVertex,
      "remove_edge", &ScriptMeshPrimitive::removeEdge,
      "remove_polygon", &ScriptMeshPrimitive::removePolygon,
      "add_shell", &ScriptMeshPrimitive::addShell,
      "add_hole", &ScriptMeshPrimitive::addHole,
      "add_island", &ScriptMeshPrimitive::addIsland,
      "fill_hole", &ScriptMeshPrimitive::fillHole,
      "slice_polygon", &ScriptMeshPrimitive::slicePolygon);

  lua.new_usertype<PrimitiveView>(
      "PrimitiveView", sol::no_constructor,

      "get_type", [](PrimitiveView const& view) { return view.primitive->getType(); },

      "get_position",
      [](PrimitiveView const& view) {
        return make_tuple(view.primitive->getPosition().x, view.primitive->getPosition().y);
      },

      "get_transform_offset",
      [](PrimitiveView const& view) {
        return make_tuple(
            view.primitive->getTransformOffset().x,
            view.primitive->getTransformOffset().y);
      },

      "get_orientation",
      [](PrimitiveView const& view) {
        return view.primitive->getOrientation();
      },

      "get_follow_orbit_angle",
      [](PrimitiveView const& view) {
        return view.primitive->getFollowOrbitAngle();
      },

      "get_influence_eye_origin_offset",
      [](PrimitiveView const& view) {
        return make_tuple(
            view.primitive->getInfluenceEyeOriginOffset().x,
            view.primitive->getInfluenceEyeOriginOffset().y);
      },
      "get_influence_eye_origin_position",
      [](PrimitiveView const& view) {
        auto const position = view.primitive->getInfluenceEyeOriginPosition();
        return make_tuple(position.x, position.y);
      },

      "get_influence_eye_angle_offset",
      [](PrimitiveView const& view) {
        return view.primitive->getInfluenceEyeAngleOffset();
      },

      "is_static",
      [](PrimitiveView const& view) {
        return view.primitive->isStatic();
      },

      "get_size",
      [](PrimitiveView const& view) {
        return make_tuple(view.primitive->getSize().x, view.primitive->getSize().y);
      },

      "get_priority",
      [](PrimitiveView const& view) {
        return static_cast<uint32_t>(view.primitive->getPriority());
      },

      "get_operation",
      [](PrimitiveView const& view) {
        return operationName(view.primitive->getOperation());
      },

      "get_floor_elevation",
      [](PrimitiveView const& view) {
        return getElevation(*view.primitive, true);
      },
      "get_ceiling_elevation",
      [](PrimitiveView const& view) {
        return getElevation(*view.primitive, false);
      },
      "get_floor_surface_material",
      [](PrimitiveView const& view) {
        return getSurfaceMaterialValues(
            *view.primitive, MaterialSurface::Floor);
      },
      "get_ceiling_surface_material",
      [](PrimitiveView const& view) {
        return getSurfaceMaterialValues(
            *view.primitive, MaterialSurface::Ceiling);
      },
      "get_wall_surface_material",
      [](PrimitiveView const& view) {
        return getSurfaceMaterialValues(
            *view.primitive, MaterialSurface::Wall);
      });

  lua.new_usertype<RunScriptContext>(
      "RunScriptContext", sol::no_constructor,
      "create_primitive", &RunScriptContext::createPrimitive,
      "create_mesh_primitive", &RunScriptContext::createMeshPrimitive,
      "place_primitive",
      sol::overload(
          &RunScriptContext::placePrimitive,
          &RunScriptContext::placeMeshPrimitive),
      "place_prefab_instance",
      [](RunScriptContext const& context, PrefabView prefab,
         sol::object const& tileX, sol::object const& tileY, float angle) {
        context.placePrefabInstance(
            prefab, tileCoordinateFromLua(tileX, "x"),
            tileCoordinateFromLua(tileY, "y"), angle);
      },
      "get_tile",
      [](RunScriptContext const& context, sol::object const& gridSize,
         float x, float y) {
        return context.getTile(prefabGridSizeFromLua(gridSize), x, y);
      },
      "find_define_prefabs", &RunScriptContext::findDefinePrefabs,
      "find_primitive_field", &RunScriptContext::findPrimitiveField,
      "find_tile_map", &RunScriptContext::findTileMap,
      "get_build_primitives",
      [](RunScriptContext const& context) {
        return sol::as_table(context.getBuildPrimitives());
      },
      "get_extents", &RunScriptContext::getExtents,
      "find_build_primitives_overlapping",
      [](RunScriptContext const& context, float x, float y, float width, float height) {
        return sol::as_table(context.findBuildPrimitivesOverlapping(
            x, y, width, height));
      });

  lua.new_usertype<PrefabVertexView>(
      "PrefabVertex", sol::no_constructor,

      "get_position",
      [](PrefabVertexView const& view) { return tuple{view.x, view.y}; },
      "get_metadata",
      [](PrefabVertexView const& view) {
        return sol::as_table(view.metadata);
      });

  lua.new_usertype<PrefabEdgeView>(
      "PrefabEdge", sol::no_constructor,

      "get_endpoints",
      [](PrefabEdgeView const& view) {
        return tuple{
            view.firstX, view.firstY, view.secondX, view.secondY};
      },
      "get_metadata",
      [](PrefabEdgeView const& view) {
        return sol::as_table(view.metadata);
      });

  lua.new_usertype<PrefabView>(
      "Prefab", sol::no_constructor,

      "get_name", [](PrefabView const& view) { return view.prefab->getName(); },
      "get_tile_size",
      [](PrefabView const& view) {
        return prefabTileSide(view.prefab->getTileSize());
      },
      "get_tags",
      [](PrefabView const& view) {
        return sol::as_table(vector<string>(
            view.prefab->getTags().begin(), view.prefab->getTags().end()));
      },
      "get_metadata_vertices",
      [](PrefabView const& view) {
        return sol::as_table(prefabMetadataVertices(*view.prefab, {}));
      },
      "get_vertices_with_metadata",
      [](PrefabView const& view, sol::table const& metadata) {
        return sol::as_table(prefabMetadataVertices(
            *view.prefab, metadataFromTable(metadata, "Vertex")));
      },
      "get_metadata_edges",
      [](PrefabView const& view) {
        return sol::as_table(prefabMetadataEdges(*view.prefab, {}));
      },
      "get_edges_with_metadata",
      [](PrefabView const& view, sol::table const& metadata) {
        return sol::as_table(prefabMetadataEdges(
            *view.prefab, metadataFromTable(metadata, "Edge")));
      });

  lua.new_usertype<DefinePrefabsView>(
      "DefinePrefabsStep", sol::no_constructor,

      "get_prefab",
      [](DefinePrefabsView const& view, string const& name) {
        auto const id = view.step->findPrefabIdByName(name);
        if (id == ~0u) {
          throw CoreException(format("No Prefab named '{}'", name));
        }
        return PrefabView{view.step->findPrefabById(id)};
      },

      "get_prefabs",
      [](DefinePrefabsView const& view) {
        return sol::as_table(toPrefabViews(view.step->getPrefabs()));
      },

      "get_prefabs_with_tags",
      [](DefinePrefabsView const& view, sol::table const& tags) {
        return sol::as_table(toPrefabViews(
            view.step->getPrefabsWithTags(prefabTagsFromTable(tags))));
      });

  lua.new_usertype<PrimitiveFieldView>(
      "PrimitiveFieldStep", sol::no_constructor,

      "get_primitives",
      [](PrimitiveFieldView const& view) {
        return sol::as_table(toPrimitiveViews(view.step->getPrimitives()));
      });

  lua.new_usertype<TileMapView>(
      "TileMap", sol::no_constructor,
      "get_index", [](TileMapView const& view) { return view.map->getIndex(); },
      "get_cell", [](TileMapView const& view, uint32_t x, uint32_t y) { return view.map->getCell(x, y); },
      "get_width", [](TileMapView const& view) { return view.map->getWidth(); },
      "get_height", [](TileMapView const& view) { return view.map->getHeight(); },
      "get_map_size", [](TileMapView const& view) { return view.map->getMapSize(); },
      "get_cell_size", [](TileMapView const& view) { return view.map->getCellSize(); });

  lua[boundMarker] = true;
}

}  // namespace core
}  // namespace bw
