#include "core-lua/RunScript.h"

#include <algorithm>
#include <format>

#include <core/CoreException.h>
#include <core/Layer.h>

#include "core-lua/ScriptBindings.h"

namespace bw {
namespace core {

using namespace std;

namespace {

// Wraps borrowed Primitive pointers as read-only PrimitiveView handles, so a
// script can read prior build Primitives but never mutate them (spec #365).
vector<PrimitiveView> asPrimitiveViews(vector<Primitive*> const& primitives) {
  vector<PrimitiveView> views;
  views.reserve(primitives.size());
  for (auto* primitive : primitives) {
    views.push_back(PrimitiveView{primitive});
  }
  return views;
}

}  // namespace

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
  result->mSeed = mSeed;
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

        // Re-seeded on every execute() so a rebuild reproduces exactly
        // (docs/adr/0040); ScriptLibraries::Build always includes math.
        environment["math"]["randomseed"](mSeed);

        environment.set_function(
            "create_primitive",
            [this](string const& type) { return createPrimitive(type); });

        environment.set_function(
            "place_primitive",
            [this, &context](Primitive* primitive) { placePrimitive(context, primitive); });

        environment.set_function(
            "get_build_primitives",
            [&context]() {
              return sol::as_table(asPrimitiveViews(context.getBuildPrimitives()));
            });

        environment.set_function(
            "get_extents",
            [&context]() {
              auto const& extents = context.getExtents();
              return make_tuple(
                  extents.getPosition().x, extents.getPosition().y,
                  extents.getSize().x, extents.getSize().y);
            });

        environment.set_function(
            "find_build_primitives_overlapping",
            [&context](float x, float y, float width, float height) {
              return sol::as_table(asPrimitiveViews(context.findBuildPrimitivesOverlapping(
                  wp::BoundingBox(x, y, width, height))));
            });
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

void RunScript::serializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->writeString("script", mScriptName);
}

bool RunScript::deserializeArgs(shared_ptr<Serializer> serializer, SerializationWorkData&) {
  mScriptName = serializer->readString("script", true);
  return true;
}

}  // namespace core
}  // namespace bw
