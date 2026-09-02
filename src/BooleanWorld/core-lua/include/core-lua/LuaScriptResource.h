#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/ResourceFactory.h>

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

inline constexpr char defaultLayerBuildStepScriptName[] =
    "__layer_build_step_script_default__";
inline constexpr char defaultLayerBuildStepScript[] =
    "-- Built-in no-op LayerBuildStep script.\n";

// A World-dependent Lua script. The host explicitly hands its source text to
// its ScriptRuntime under the exact name authored by the RunScript step before
// deserializing the World. A script may come from a ResourceLocation or be an
// internal, programmatic resource such as the built-in no-op default.
class LuaScriptResource final
    : public wp::application::resourcesystem::Resource {
private:
  std::string mText;
  std::string mInternalText;

  void create(
      wp::application::resourcesystem::DataStreamPtr data,
      wp::application::resourcesystem::ResourceManager* resourceManager) override;
  void parseData(
      wp::application::resourcesystem::DataStreamPtr data) override;
  void destroy() override;

public:
  LuaScriptResource(
      std::string const& name, std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location);

  // Constructs an internal script whose text does not come from a location.
  LuaScriptResource(
      std::string const& name, std::string const& namesp,
      std::string text);

  [[nodiscard]] std::string const& getText() const;

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

// Registers the Resource factory and the built-in no-op default with one
// host's Willpower ResourceManager. This must happen before its locations are
// scanned, because manifests may contain resources of type LuaScript.
void registerLuaScriptResourceType(
    wp::application::resourcesystem::ResourceManager& resourceManager);

}  // namespace core
}  // namespace bw
