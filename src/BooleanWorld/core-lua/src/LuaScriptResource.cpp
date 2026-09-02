#include "core-lua/LuaScriptResource.h"

#include <willpower/application/resourcesystem/ResourceManager.h>

#include "core-lua/ScriptRuntime.h"

namespace bw {
namespace core {

using namespace std;
namespace resources = wp::application::resourcesystem;

LuaScriptResource::LuaScriptResource(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags, resources::ResourceLocation* location)
    : TextFileResource(name, namesp, source, tags, location) {
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
}

}  // namespace core
}  // namespace bw
