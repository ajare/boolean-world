#include "core-lua/RunScript.h"

#include <algorithm>
#include <format>
#include <set>

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
    LayerBuildContext& context, Prefab const* prefab, float x, float y, float angle) const {
  if (!prefab) {
    throw CoreException("A script placed an instance of an unknown Prefab");
  }

  map<VertexTransformerObject const*, VertexTransformerObject*> instanceClones;
  vector<Primitive*> clones;
  clones.reserve(prefab->getPrimitives().size());
  for (auto const* source : prefab->getPrimitives()) {
    unique_ptr<Primitive> clone(source->rotatedCopy(angle));
    clone->setPosition(clone->getPosition() + wp::Vector2(x, y));
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

          // One explicit, borrowed capability object owns every operation
          // scoped to this RunScript execution. The actual RunScript step and
          // its authored configuration are not exposed to Lua.
          environment["context"] = RunScriptContext(*this, context);
        });
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
  return true;
}

}  // namespace core
}  // namespace bw
