#include "core-lua/LuaScriptResource.h"

#include <memory>
#include <utility>

#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/application/resourcesystem/TextFileResource.h>

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
    resources::DataStreamPtr data, resources::ResourceManager* resourceManager) {
  // LuaScript has no structured ResourceDefinition. A leaf reads its source
  // directly, a composite reads the named TextFile dependency "Source", and
  // a built-in uses its internal text.
  mResourceManager = resourceManager;
  if (data) {
    parseData(move(data));
  } else if (hasDependentResource("Source")) {
    auto source = dynamic_pointer_cast<resources::TextFileResource>(
        getDependentResource("Source"));
    if (!source) {
      throw resources::ResourceException(
          this, "LuaScript dependency 'Source' is not a TextFile resource.");
    }
    mText = source->getText();
  } else {
    parseData({});
  }
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
  mResourceManager = nullptr;
}

string const& LuaScriptResource::getText() const {
  return mText;
}

map<string, string> LuaScriptResource::collectIncludedScripts() const {
  map<string, string> scripts;
  if (!mResourceManager) {
    return scripts;
  }

  // ResourceManager creates dependencies before their parent, so every text
  // below is ready by the time the parent is handed to ScriptRuntime. Expose
  // only manifest-declared transitive LuaScript dependencies, under canonical
  // qualified names such as "World/Foo".
  for (auto const& candidate :
       mResourceManager->getResourcesByType("LuaScript")) {
    if (candidate.get() == this || !dependsOn(candidate.get())) {
      continue;
    }
    auto script = dynamic_pointer_cast<LuaScriptResource>(candidate);
    if (script) {
      scripts.insert_or_assign(candidate->getQualifiedName(), script->getText());
    }
  }
  return scripts;
}

void LuaScriptResource::loadInto(
    ScriptRuntime& runtime, string const& authoredName) const {
  runtime.load(authoredName, getText(), collectIncludedScripts());
}

void LuaScriptResource::reloadInto(
    ScriptRuntime& runtime, string const& authoredName) const {
  runtime.reload(authoredName, getText(), collectIncludedScripts());
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
