#include "core-lua/LuaScriptResource.h"

#include <memory>
#include <utility>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include "core-lua/ScriptRuntime.h"

namespace bw {
namespace core {

using namespace std;
namespace resources = wp::application::resourcesystem;

LuaScriptResource::LuaScriptResource(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, resources::ResourceLocation* location)
    : Resource(name, namesp, "LuaScript", source, tags, location) {
}

LuaScriptResource::LuaScriptResource(
    string const& name, string const& namesp, string text)
    : Resource(name, namesp, "LuaScript", "", {}, nullptr),
      mInternalText(move(text)) {
}

void LuaScriptResource::create(
    resources::DataStreamPtr data, resources::ResourceManager*) {
  // LuaScript has no structured ResourceDefinition; its complete definition
  // is the source text itself (or the internal text for a built-in).
  parseData(move(data));
}

void LuaScriptResource::parseData(resources::DataStreamPtr data) {
  if (data) {
    mText.assign(
        reinterpret_cast<char const*>(data->getData()), data->getSize());
  } else {
    mText = mInternalText;
  }
}

void LuaScriptResource::destroy() {
  mText.clear();
}

string const& LuaScriptResource::getText() const {
  return mText;
}

void LuaScriptResource::loadInto(
    ScriptRuntime& runtime, string const& authoredName) const {
  runtime.load(authoredName, getText());
}

LuaScriptResourceFactory::LuaScriptResourceFactory()
    : ResourceFactory("LuaScript") {
}

resources::Resource* LuaScriptResourceFactory::createResource(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, resources::ResourceLocation* location) {
  return new LuaScriptResource(name, namesp, source, tags, location);
}

void registerLuaScriptResourceType(resources::ResourceManager& resourceManager) {
  resourceManager.addResourceFactory(new LuaScriptResourceFactory);
  resourceManager.addResource(make_shared<LuaScriptResource>(
      defaultLayerBuildStepScriptName, "World", defaultLayerBuildStepScript));
}

}  // namespace core
}  // namespace bw
