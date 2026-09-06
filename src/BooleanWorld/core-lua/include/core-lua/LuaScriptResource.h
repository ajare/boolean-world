#pragma once

#include <map>
#include <string>
#include <vector>

#include <willpower/application/resourcesystem/ResourceFactory.h>

#include "core-lua/ScriptParameters.h"

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

// A World-dependent Lua script. The host explicitly hands its source text,
// manifest-declared Params, and transitive LuaScript dependencies to
// ScriptRuntime under the exact name authored by RunScript before deserializing
// the World. A leaf
// script may come directly from a ResourceLocation. A composite root obtains
// its source from the named TextFile dependency "Source" and may depend on
// helper LuaScripts. Internal programmatic scripts need neither.
class LuaScriptResource final
    : public wp::application::resourcesystem::Resource {
private:
  std::string mText;
  std::string mInternalText;
  std::vector<ScriptParameterDefinition> mParameterDefinitions;
  wp::application::resourcesystem::ResourceManager* mResourceManager = nullptr;

  [[nodiscard]] std::map<std::string, std::string> collectIncludedScripts() const;

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

  [[nodiscard]] std::vector<ScriptParameterDefinition> const&
  getParameterDefinitions() const;

  // Used by the ResourceDefinitionFactory after validating manifest Params.
  void setParameterDefinitions(
      std::vector<ScriptParameterDefinition> definitions);

  void loadInto(ScriptRuntime& runtime,
                std::string const& authoredName) const;

  // As loadInto(), but also rebuilds every Layer whose RunScript names this
  // root after atomically replacing its compiled form.
  void reloadInto(ScriptRuntime& runtime,
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
