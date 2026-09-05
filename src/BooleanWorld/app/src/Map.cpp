#include <algorithm>
#include <cctype>

#include <willpower/application/resourcesystem/TextFileResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/BinarySerializer.h>
#include <core/CoreException.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/YamlSerializer.h>
#include <core-lua/CoreLua.h>

#include "Map.h"

using namespace std;
using namespace wp;
using namespace wp::geometry;

namespace {
bool hasExtension(string const& filepath, string const& extension) {
  if (filepath.size() < extension.size()) return false;

  auto const tail = filepath.substr(filepath.size() - extension.size());
  return equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return tolower(static_cast<unsigned char>(a)) ==
           tolower(static_cast<unsigned char>(b));
  });
}
}  // namespace

Map::Map(string const& name,
         string const& namesp,
         string const& source,
         map<string, string> const& tags,
         application::resourcesystem::ResourceLocation* location,
         wp::Logger* logger,
         bw::core::ScriptRuntime* scriptRuntime)
    : applib::Map(name, namesp, source, tags, location, 512),
      mWorld(nullptr),
      mwLogger(logger),
      mScriptRuntime(scriptRuntime) {
}

Map::~Map() {
  delete mWorld;
}

bw::core::World* Map::getWorld() {
  return mWorld;
}

bw::core::World const* Map::getWorld() const {
  return mWorld;
}

void Map::loadWorldFromYaml(
    wp::application::resourcesystem::ResourcePtr resource,
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  auto res = static_cast<wp::application::resourcesystem::TextFileResource*>(resource.get());
  string text = res->getText();

  // The resource's source carries the original filename. YAML Worlds require
  // the compound .world.yaml extension; binary Worlds retain .world.
  auto const& source = resource->getSource();
  auto const yaml = hasExtension(source, ".world.yaml");
  auto const binary = hasExtension(source, ".world") && !yaml;
  if (!yaml && !binary) {
    throw wp::application::resourcesystem::ResourceException(
        resource.get(), "World files must end in .world.yaml or .world.");
  }

  shared_ptr<bw::core::Serializer> ser = binary
                                             ? shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromString(text))
                                             : shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromString(text));

  ser->deserialize();

  auto dependencySerializer = binary
                                  ? shared_ptr<bw::core::Serializer>(
                                        bw::core::BinarySerializer::fromString(text))
                                  : shared_ptr<bw::core::Serializer>(
                                        bw::core::YamlSerializer::fromString(text));
  dependencySerializer->deserialize();
  auto dependencyNames =
      bw::core::World::readDependentResourceNames(dependencySerializer);
  vector<wp::application::resourcesystem::ResourcePtr> acquiredDependencies;
  try {
    for (auto const& reference : dependencyNames) {
      if (!resourceMgr) continue;
      string namesp;
      string name;
      wp::application::resourcesystem::Resource::splitName(
          reference, getNamespace(), &namesp, &name);
      auto dependency = resourceMgr->getResource(name, namesp);
      if (dependency.get() == this || dependency == resource ||
          dependency->dependsOn(this)) {
        throw wp::application::resourcesystem::ResourceException(
            this, "World dependent resources may not include their Map or source file.");
      }
      dependency = resourceMgr->acquireResource(name, namesp);
      try {
        resourceMgr->createResource(dependency);
        resourceMgr->loadResource(dependency);
        if (auto script = dynamic_pointer_cast<bw::core::LuaScriptResource>(
                dependency)) {
          if (!mScriptRuntime) {
            throw bw::core::CoreException(
                "The Map host has no ScriptRuntime for a LuaScript resource");
          }
          try {
            script->loadInto(*mScriptRuntime, reference);
          } catch (bw::core::ScriptException const& error) {
            // The runtime retains compile failures by name. Let the World
            // deserialize so its RunScript step can expose the failure in the
            // same way as an execution error (#367).
            mwLogger->error(error.what());
          }
        }
      } catch (...) {
        resourceMgr->releaseResource(dependency);
        throw;
      }
      acquiredDependencies.push_back(move(dependency));
    }
  } catch (...) {
    for (auto const& dependency : acquiredDependencies) {
      resourceMgr->releaseResource(dependency);
    }
    throw;
  }
  for (auto const& dependency : acquiredDependencies) {
    addDependentResource(dependency);
  }

  auto candidate = std::make_unique<bw::core::World>(1.0f, -1.0f);

  // Create grid with cell size 512
  auto workData = bw::core::SerializationWorkData{512.0f};

  if (candidate->deserialize(ser, workData)) {
    auto const& warnings = candidate->getDeserializationWarnings();

    if (!warnings.empty()) {
      for (auto const& warning : warnings) {
        mwLogger->warn(warning);
      }
    }

    candidate->setWorldDataGenerator(
        new bw::core::DynamicWorldDataGenerator(candidate.get(), false));
    delete mWorld;
    mWorld = candidate.release();
  } else {
    auto const& errors = candidate->getDeserializationErrors();

    if (!errors.empty()) {
      for (auto const& error : errors) {
        mwLogger->error(error);
      }
    }

    throw wp::application::resourcesystem::ResourceException(resource.get(), "Could not load World from YAML.");
  }
}
