#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/Logger.h>

#include <core/LayerBuildStep.h>
#include <core/World.h>
#include <core/YamlSerializer.h>
#include <core-lua/CoreLua.h>
#include <core-lua/RunScript.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

std::string serializeScriptWorld() {
  bw::core::ScriptRuntime sourceRuntime;
  sourceRuntime.load("ScriptDemo", R"(
    local primitive = context:create_primitive("Rectangle")
    primitive:set_size(8, 8)
    context:place_primitive(primitive)
  )");

  bw::core::World source(512.0f, 16.0f);
  auto* step = new bw::core::RunScript(sourceRuntime);
  step->setScriptName("ScriptDemo");
  source.getActiveLayer()->addStep(step);

  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  auto workData = bw::core::SerializationWorkData{};
  source.serialize(serializer, workData);
  return serializer->getSerializedString();
}

bool deserializeWorld(std::string const& yaml, bw::core::World* world) {
  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();
  auto workData = bw::core::SerializationWorkData{};
  return world->deserialize(serializer, workData);
}

void anUnregisteredHostFailsNamingRunScript(std::string const& yaml) {
  bw::core::World world(512.0f, 16.0f);
  require(!deserializeWorld(yaml, &world),
          "a host without RunScript registration opened a script World");

  for (auto const& error : world.getDeserializationErrors()) {
    if (error.find("RunScript") != std::string::npos) return;
  }
  throw std::runtime_error(
      "the unregistered-step failure did not name RunScript");
}

void aHostResolvesAndCompilesScriptsBeforeWorldDeserialization(
    std::string const& yaml) {
  namespace fs = std::filesystem;
  namespace resources = wp::application::resourcesystem;

  auto const unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
  auto root = fs::temp_directory_path() /
              ("boolean-world-lua-resource-" + std::to_string(unique));
  fs::create_directories(root);
  {
    std::ofstream script(root / "script-demo.lua");
    script << R"(
      local primitive = context:create_primitive("Rectangle")
      primitive:set_size(8, 8)
      context:place_primitive(primitive)
    )";
    std::ofstream manifest(root / "Resources.yaml");
    manifest << R"(Resources:
  Namespace:
    name: "World"
    Resource:
      type: "LuaScript"
      name: "ScriptDemo"
      location: "script-demo.lua"
)";
  }

  wp::Logger logger;
  resources::ResourceManager resourceManager(nullptr, nullptr, nullptr,
                                             &logger);
  resourceManager.addResourceLocationFactory(
      "Directory",
      [&logger](std::string const& path,
                std::string const& definition) -> resources::ResourceLocation* {
        return new resources::DirectoryResourceLocation(
            &logger, path, definition);
      });
  bw::core::registerLuaScriptResourceType(resourceManager);
  resourceManager.addResourceLocation(
      "Directory", root.string(), "Resources.yaml");
  resourceManager.scanLocations();

  auto resource = resourceManager.getResource("ScriptDemo", "World");
  require(dynamic_cast<bw::core::LuaScriptResource*>(resource.get()),
          "the LuaScript factory did not create a LuaScriptResource");

  // This is the ADR-0033 host sequence: inspect only the dependency header,
  // resolve and compile each LuaScript under its exact authored spelling,
  // then permit full World deserialization and its rebuild.
  auto dependencyReader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(yaml));
  dependencyReader->deserialize();
  auto dependencies =
      bw::core::World::readDependentResourceNames(dependencyReader);
  require(dependencies == std::vector<std::string>{"ScriptDemo"},
          "the script World did not declare its LuaScript dependency");

  bw::core::ScriptRuntime hostRuntime;
  for (auto const& reference : dependencies) {
    std::string namesp;
    std::string name;
    resources::Resource::splitName(reference, "World", &namesp, &name);
    auto dependency = resourceManager.acquireResource(name, namesp);
    resourceManager.createResource(dependency);
    resourceManager.loadResource(dependency);
    auto* script = dynamic_cast<bw::core::LuaScriptResource*>(dependency.get());
    require(script, "the declared script resolved to a non-Lua resource");
    script->loadInto(hostRuntime, reference);
  }
  require(hostRuntime.isLoaded("ScriptDemo"),
          "resolved LuaScript text was not compiled into the host runtime");

  bw::core::registerScriptStepTypes(hostRuntime);
  bw::core::World loaded(512.0f, 16.0f);
  require(deserializeWorld(yaml, &loaded),
          "the host could not deserialize a World after resolving its script");
  require(loaded.getActiveLayer()->getNumPrimitives() == 1,
          "opening the script World did not run it and produce its Primitive");

  fs::remove_all(root);
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    auto yaml = serializeScriptWorld();
    anUnregisteredHostFailsNamingRunScript(yaml);
    aHostResolvesAndCompilesScriptsBeforeWorldDeserialization(yaml);
    std::cout << "LuaScript resource host integration tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
