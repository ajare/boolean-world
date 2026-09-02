#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/ResourceFactory.h>
#include <willpower/application/resourcesystem/TextFileResource.h>

namespace wp {
namespace application {
namespace resourcesystem {
class ResourceManager;
}  // namespace resourcesystem
}  // namespace application
}  // namespace wp

namespace bw {
namespace core {

class ScriptRuntime;

// A World-dependent Lua script. TextFileResource owns the source text; the
// host explicitly hands that text to its ScriptRuntime under the exact name
// authored by the RunScript step before deserializing the World.
class LuaScriptResource final
    : public wp::application::resourcesystem::TextFileResource {
public:
  LuaScriptResource(
      std::string const& name, std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location);

  void loadInto(ScriptRuntime& runtime,
                std::string const& authoredName) const;
};

class LuaScriptResourceFactory final
    : public wp::application::resourcesystem::ResourceFactory {
public:
  LuaScriptResourceFactory();

  wp::application::resourcesystem::Resource* createResource(
      std::string const& name, std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override;
};

// Registers the Resource factory with one host's Willpower ResourceManager.
// This must happen before its locations are
// scanned, because manifests may contain resources of type LuaScript.
void registerLuaScriptResourceType(
    wp::application::resourcesystem::ResourceManager& resourceManager);

}  // namespace core
}  // namespace bw
