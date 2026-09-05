#pragma once

#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <willpower/common/Logger.h>

#include <applib/Map.h>

#include <core/World.h>

namespace bw {
namespace core {
class ScriptRuntime;
}  // namespace core
}  // namespace bw

class Map : public applib::Map {
  bw::core::World* mWorld;

  wp::Logger* mwLogger;
  bw::core::ScriptRuntime* mScriptRuntime;

public:
  Map(std::string const& name,
      std::string const& namesp,
      std::string const& source,
      std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location,
      wp::Logger* logger,
      bw::core::ScriptRuntime* scriptRuntime = nullptr);

  ~Map();

  bw::core::World* getWorld();

  bw::core::World const* getWorld() const;

  void loadWorldFromYaml(
      wp::application::resourcesystem::ResourcePtr resource,
      wp::application::resourcesystem::ResourceManager* resourceMgr = nullptr);
};

class MapResourceFactory : public wp::application::resourcesystem::ResourceFactory {
  wp::Logger* mwLogger;
  bw::core::ScriptRuntime* mScriptRuntime;

public:
  MapResourceFactory(wp::Logger* logger, bw::core::ScriptRuntime& scriptRuntime)
      : wp::application::resourcesystem::ResourceFactory("Map"),
        mwLogger(logger),
        mScriptRuntime(&scriptRuntime) {
  }

  wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override {
    return new Map(name, namesp, source, tags, location, mwLogger,
                   mScriptRuntime);
  }
};