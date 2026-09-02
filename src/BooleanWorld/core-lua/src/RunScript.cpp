#include "core-lua/RunScript.h"

#include <algorithm>
#include <format>

#include <core/CoreException.h>
#include <core/Layer.h>

#include "core-lua/ScriptBindings.h"

namespace bw {
namespace core {

using namespace std;

RunScript::RunScript(ScriptRuntime& runtime)
    : mRuntime(&runtime) {
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
  return result;
}

Primitive* RunScript::createPrimitive(string const& type) const {
  auto primitive = unique_ptr<Primitive>(Primitive::instantiate(type));
  auto* borrowed = primitive.get();
  mBuiltPrimitives.push_back(move(primitive));
  return borrowed;
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

void RunScript::execute(LayerBuildContext& context) const {
  mBuiltPrimitives.clear();
  mPlacedPrimitives.clear();

  if (mScriptName.empty()) {
    throw CoreException("A RunScript step names no Lua script");
  }

  mRuntime->execute(
      mScriptName, ScriptLibraries::Build, [this, &context](sol::environment& environment) {
        bindScriptTypes(mRuntime->getState());

        environment.set_function(
            "create_primitive",
            [this](string const& type) { return createPrimitive(type); });

        environment.set_function(
            "place_primitive",
            [this, &context](Primitive* primitive) { placePrimitive(context, primitive); });
      });
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

void RunScript::setScriptName(string const& name) {
  if (name == mScriptName) {
    return;
  }

  mScriptName = name;
  modify();
}

string const& RunScript::getScriptName() const {
  return mScriptName;
}

ScriptRuntime& RunScript::getRuntime() const {
  return *mRuntime;
}

void RunScript::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeString("script", mScriptName);
}

bool RunScript::deserializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) {
  mScriptName = serializer->readString("script", true);
  return true;
}

}  // namespace core
}  // namespace bw
