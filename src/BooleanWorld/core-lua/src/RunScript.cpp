#include "core-lua/RunScript.h"

#include <algorithm>
#include <format>
#include <set>
#include <type_traits>
#include <variant>

#include <core/CoreException.h>
#include <core/DefinePrefabs.h>
#include <core/Layer.h>

#include "core-lua/LuaScriptResource.h"
#include "core-lua/ScriptBindings.h"

namespace bw {
namespace core {

using namespace std;

RunScript::RunScript(ScriptRuntime& runtime)
    : mRuntime(&runtime),
      mScriptName(defaultLayerBuildStepScriptName) {
}

RunScript::~RunScript() {
  mRuntime->untrackStep(this, mScriptName);
}

void RunScript::owningLayerChanged(Layer*, Layer* newLayer) {
  mRuntime->untrackStep(this, mScriptName);
  mRuntime->trackStep(this, mScriptName, newLayer);
}

string RunScript::getType() const {
  return "RunScript";
}

bool RunScript::mayBeFirstStep() const {
  return false;
}

LayerBuildStep* RunScript::copy(map<VertexTransformerObject const*, VertexTransformerObject*>&) const {
  // The built Primitives are derived output, not authored state: the copy
  // produces its own by running the script when its Layer is next rebuilt.
  auto* result = new RunScript(*mRuntime);
  result->copyFrom(*this);
  result->mScriptName = mScriptName;
  result->mExtraResourceNames = mExtraResourceNames;
  result->mParameterValues = mParameterValues;
  result->mSeed = mSeed;
  return result;
}

Primitive* RunScript::ownPrimitive(unique_ptr<Primitive> primitive) const {
  auto* borrowed = primitive.get();
  mBuiltPrimitives.push_back(move(primitive));
  return borrowed;
}

Primitive* RunScript::createPrimitive(string const& type) const {
  return ownPrimitive(unique_ptr<Primitive>(Primitive::createDefault(type)));
}

string RunScript::nextAudioEmitterGuid() const {
  // SplitMix64 gives a stable cross-platform mapping from the serialized seed
  // and creation order. UUID version/variant bits are set only to keep the
  // resulting opaque identity in the conventional textual shape.
  auto mix = [](uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
  };

  auto const counter = mAudioEmitterCounter++;
  auto const high = mix(mSeed ^ mix(counter));
  auto const low = mix(high ^ counter ^ 0xd6e8feb86659fd93ull);
  return format(
      "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
      static_cast<uint32_t>(high >> 32),
      static_cast<uint16_t>(high >> 16),
      static_cast<uint16_t>((high & 0x0fffull) | 0x5000ull),
      static_cast<uint16_t>(((low >> 48) & 0x3fffull) | 0x8000ull),
      low & 0x0000ffffffffffffull);
}

void RunScript::placePrimitive(LayerBuildContext& context, Primitive* primitive) const {
  if (!primitive) {
    throw CoreException("A script placed nothing");
  }

  if (!ownsPrimitive(primitive)) {
    throw CoreException(
        "A script placed a Primitive its RunScript step does not own");
  }

  if (find(mPlacedPrimitives.begin(), mPlacedPrimitives.end(), primitive) !=
      mPlacedPrimitives.end()) {
    throw CoreException("A script placed the same Primitive twice");
  }

  mPlacedPrimitives.push_back(primitive);
  context.appendPrimitive(primitive);
}

void RunScript::placePrefabInstance(
    LayerBuildContext& context, Prefab const* prefab,
    int32_t tileX, int32_t tileY, float angle) const {
  if (!prefab) {
    throw CoreException("A script placed an instance of an unknown Prefab");
  }
  if (angle != 0.0f && angle != 90.0f &&
      angle != 180.0f && angle != 270.0f) {
    throw CoreException(
        "A Prefab instance angle must be 0, 90, 180, or 270 degrees");
  }

  auto const side = static_cast<float>(prefabTileSide(prefab->getTileSize()));
  auto const position = wp::Vector2{
      (static_cast<float>(tileX) + 0.5f) * side,
      (static_cast<float>(tileY) + 0.5f) * side};

  map<VertexTransformerObject const*, VertexTransformerObject*> instanceClones;
  vector<Primitive*> clones;
  clones.reserve(prefab->getPrimitives().size());
  for (auto const* source : prefab->getPrimitives()) {
    unique_ptr<Primitive> clone(source->rotatedCopy(angle));
    clone->setPosition(clone->getPosition() + position);
    clone->setEmitterPlacementKey(EmitterPlacementKey{
        tileX, tileY, prefabTileSide(prefab->getTileSize())});
    auto* raw = clone.get();
    instanceClones[source] = raw;
    mBuiltPrimitives.push_back(move(clone));
    clones.push_back(raw);
  }

  for (size_t index = 0; index < prefab->getPrimitives().size(); ++index) {
    auto const* source = prefab->getPrimitives()[index];
    auto* parent = source->getParent();
    clones[index]->setParent(
        parent && instanceClones.contains(parent) ? instanceClones[parent] : nullptr);
  }

  for (auto* clone : clones) {
    mPlacedPrimitives.push_back(clone);
    context.appendPrimitive(clone);
  }
}

void RunScript::execute(LayerBuildContext& context) const {
  mBuiltPrimitives.clear();
  mPlacedPrimitives.clear();
  mAudioEmitterCounter = 0;
  mFailureLineNumber = 0;
  mFailureTraceback.clear();

  if (mScriptName.empty()) {
    throw CoreException("A RunScript step names no Lua script");
  }

  try {
    mRuntime->execute(
        mScriptName, ScriptLibraries::Build, [this, &context](sol::environment& environment) {
          bindScriptTypes(mRuntime->getState());

          // Re-seeded on every execute() so a rebuild reproduces exactly
          // (docs/adr/0040); ScriptLibraries::Build always includes math.
          environment["math"]["randomseed"](mSeed);

          sol::table params = environment["params"];
          for (auto const& definition :
               mRuntime->getParameterDefinitions(mScriptName)) {
            auto const& value = getParameterValue(definition);
            if (!definition.accepts(value)) {
              throw CoreException(format(
                  "RunScript parameter '{}' is not a valid {} value for its "
                  "LuaScript resource definition",
                  definition.name, scriptParameterTypeName(definition.type)));
            }
            visit(
                [&params, &definition](auto const& concrete) {
                  params[definition.name] = concrete;
                },
                value);
          }
          environment["params"] = params;

          // One explicit, borrowed capability object owns every operation
          // scoped to this RunScript execution. The actual RunScript step and
          // its authored configuration other than params are not exposed to Lua.
          environment["context"] = RunScriptContext(*this, context);
        },
        getName());
  } catch (ScriptException const& error) {
    mFailureLineNumber = error.getLineNumber();
    mFailureTraceback = error.getTraceback();
    throw;
  }
}

bool RunScript::primitivesParticipateInBuild() const {
  return true;
}

bool RunScript::permitsDirectPrimitiveEditing() const {
  return false;
}

bool RunScript::acceptsNewPrimitives() const {
  return false;
}

uint32_t RunScript::adoptPrimitive(Primitive*) {
  throw CoreException("RunScript does not accept Primitives");
}

void RunScript::replacePrimitive(Primitive*, Primitive*) {
  throw CoreException("RunScript output cannot be edited directly");
}

void RunScript::releasePrimitive(Primitive*) {
  throw CoreException("RunScript output cannot be moved to another step");
}

bool RunScript::ownsPrimitive(Primitive const* primitive) const {
  return any_of(mBuiltPrimitives.begin(), mBuiltPrimitives.end(),
                [primitive](auto const& built) { return built.get() == primitive; });
}

vector<string> RunScript::collectDependentResourceNames() const {
  set<string> names;
  if (!mScriptName.empty()) {
    names.insert(mScriptName);
  }
  for (auto const& name : mExtraResourceNames) {
    if (!name.empty()) {
      names.insert(name);
    }
  }
  return {names.begin(), names.end()};
}

void RunScript::setScriptName(string const& name) {
  if (name == mScriptName) {
    return;
  }

  mRuntime->untrackStep(this, mScriptName);
  mScriptName = name;
  mParameterValues.clear();
  mRuntime->trackStep(this, mScriptName, getOwningLayer());
  modify();
}

string const& RunScript::getScriptName() const {
  return mScriptName;
}

void RunScript::setExtraResourceNames(vector<string> names) {
  if (names == mExtraResourceNames) {
    return;
  }

  mExtraResourceNames = move(names);
  modify();
}

vector<string> const& RunScript::getExtraResourceNames() const {
  return mExtraResourceNames;
}

void RunScript::setParameterValue(
    string const& name, ScriptParameterValue const& value) {
  auto const& definitions = mRuntime->getParameterDefinitions(mScriptName);
  auto const definition = find_if(
      definitions.begin(), definitions.end(),
      [&name](auto const& candidate) { return candidate.name == name; });
  if (definition == definitions.end()) {
    throw CoreException(
        format("LuaScript '{}' declares no Param named '{}'", mScriptName, name));
  }
  if (!definition->accepts(value)) {
    throw CoreException(format(
        "RunScript parameter '{}' is not a valid {} value for its LuaScript "
        "resource definition",
        name, scriptParameterTypeName(definition->type)));
  }
  if (auto current = mParameterValues.find(name);
      current != mParameterValues.end() && current->second == value) {
    return;
  }
  mParameterValues.insert_or_assign(name, value);
  modify();
}

void RunScript::clearParameterValue(string const& name) {
  if (mParameterValues.erase(name) != 0) {
    modify();
  }
}

map<string, ScriptParameterValue> const& RunScript::getParameterValues() const {
  return mParameterValues;
}

ScriptParameterValue const& RunScript::getParameterValue(
    ScriptParameterDefinition const& definition) const {
  auto const found = mParameterValues.find(definition.name);
  return found == mParameterValues.end() ? definition.defaultValue
                                         : found->second;
}

void RunScript::setSeed(uint64_t seed) {
  if (seed == mSeed) {
    return;
  }

  mSeed = seed;
  modify();
}

uint64_t RunScript::getSeed() const {
  return mSeed;
}

ScriptRuntime& RunScript::getRuntime() const {
  return *mRuntime;
}

uint32_t RunScript::getFailureLineNumber() const {
  return hasFailed() ? mFailureLineNumber : 0;
}

string const& RunScript::getFailureTraceback() const {
  static string const empty;
  return hasFailed() ? mFailureTraceback : empty;
}

void RunScript::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeString("script", mScriptName);
  serializer->writeUint64("seed", mSeed);
  serializer->beginArray("extraResources");
  for (auto const& name : mExtraResourceNames) {
    serializer->writeString("", name);
  }
  serializer->endArray();

  auto values = mParameterValues;
  for (auto const& definition :
       mRuntime->getParameterDefinitions(mScriptName)) {
    values.try_emplace(definition.name, definition.defaultValue);
  }
  serializer->beginArray("params");
  for (auto const& [name, value] : values) {
    serializer->beginMap("");
    serializer->writeString("name", name);
    visit(
        [&serializer](auto const& concrete) {
          using Value = decay_t<decltype(concrete)>;
          if constexpr (is_same_v<Value, string>) {
            serializer->writeString("type", "string");
            serializer->writeString("value", concrete);
          } else if constexpr (is_same_v<Value, int64_t>) {
            serializer->writeString("type", "integer");
            serializer->writeInt64("value", concrete);
          } else if constexpr (is_same_v<Value, double>) {
            serializer->writeString("type", "number");
            serializer->writeDouble("value", concrete);
          } else {
            serializer->writeString("type", "boolean");
            serializer->writeBool("value", concrete);
          }
        },
        value);
    serializer->endMap();
  }
  serializer->endArray();
}

bool RunScript::deserializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) {
  mScriptName = serializer->readString("script", true);
  mSeed = serializer->readUint64("seed");
  mExtraResourceNames.clear();
  serializer->beginArray("extraResources");
  while (serializer->nextArrayItem()) {
    mExtraResourceNames.push_back(serializer->readString());
  }
  serializer->endArray();

  mParameterValues.clear();
  if (serializer->isPositional() || serializer->hasField("params")) {
    serializer->beginArray("params");
    while (serializer->nextArrayItem()) {
      serializer->beginMap("");
      auto const name = serializer->readString("name");
      auto const type = serializer->readString("type");
      if (type == "string") {
        mParameterValues.insert_or_assign(
            name, serializer->readString("value"));
      } else if (type == "integer") {
        mParameterValues.insert_or_assign(
            name, serializer->readInt64("value"));
      } else if (type == "number") {
        mParameterValues.insert_or_assign(
            name, serializer->readDouble("value"));
      } else if (type == "boolean") {
        mParameterValues.insert_or_assign(
            name, serializer->readBool("value"));
      } else {
        throw CoreException(
            format("RunScript parameter '{}' has unknown serialized type '{}'",
                   name, type));
      }
      serializer->endMap();
    }
    serializer->endArray();
  }
  return true;
}

}  // namespace core
}  // namespace bw
