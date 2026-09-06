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
  bw::core::ScriptParameterDefinition count;
  count.name = "count";
  count.type = bw::core::ScriptParameterType::Integer;
  count.defaultValue = int64_t{4};
  count.integerMinimum = 1;
  count.integerMaximum = 20;
  sourceRuntime.load("ScriptDemo", R"(
    local primitive = context:create_primitive("Rectangle")
    primitive:set_size(8, 8)
    context:place_primitive(primitive)
  )",
                     {}, {count});

  bw::core::World source(512.0f, 16.0f);
  auto* step = new bw::core::RunScript(sourceRuntime);
  step->setScriptName("ScriptDemo");
  step->setParameterValue("count", int64_t{9});
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
      local dimensions = include("World/ScriptDimensions")
      local primitive = context:create_primitive("Rectangle")
      primitive:set_size(dimensions.width + params.count,
                         dimensions.height * params.scale)
      context:place_primitive(primitive)
    )";
    std::ofstream dimensions(root / "script-dimensions.lua");
    dimensions << "return { width = 8, height = 8 }\n";
    std::ofstream manifest(root / "Resources.yaml");
    manifest << R"(Resources:
  Namespace:
    name: "World"
    Resource:
      - type: "TextFile"
        name: "ScriptDemoSource"
        location: "script-demo.lua"
      - type: "LuaScript"
        name: "ScriptDimensions"
        location: "script-dimensions.lua"
      - type: "LuaScript"
        name: "ScriptDemo"
        DependentResources:
          DependentResource:
            - id: "Source"
              ref: "ScriptDemoSource"
            - ref: "ScriptDimensions"
        Definitions:
          Definition:
            Params:
              Param:
                - name: "label"
                  type: "string"
                  default: "mine"
                - name: "style"
                  type: "string"
                  default: "rough"
                  Choices:
                    Choice:
                      - "rough"
                      - "smooth"
                - name: "count"
                  type: "integer"
                  min: "1"
                  max: "20"
                  default: "4"
                - name: "scale"
                  type: "number"
                  min: "0.25"
                  max: "4.0"
                  default: "1.5"
                - name: "enabled"
                  type: "boolean"
                  default: "true"
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
  require(resource->getType() == "LuaScript" &&
              resourceManager.getResourcesByType("LuaScript").size() == 3,
          "LuaScript resources were not discoverable by their declared type");
  resourceManager.createResource(resource);
  resourceManager.loadResource(resource);
  auto* parameterizedScript =
      dynamic_cast<bw::core::LuaScriptResource*>(resource.get());
  auto const& parameterDefinitions =
      parameterizedScript->getParameterDefinitions();
  require(
      parameterDefinitions.size() == 5 &&
          parameterDefinitions[0].name == "label" &&
          std::get<std::string>(parameterDefinitions[0].defaultValue) ==
              "mine" &&
          parameterDefinitions[1].choices ==
              std::vector<std::string>{"rough", "smooth"} &&
          parameterDefinitions[2].integerMinimum == 1 &&
          parameterDefinitions[2].integerMaximum == 20 &&
          std::get<int64_t>(parameterDefinitions[2].defaultValue) == 4 &&
          parameterDefinitions[3].numberMinimum == 0.25 &&
          parameterDefinitions[3].numberMaximum == 4.0 &&
          std::get<double>(parameterDefinitions[3].defaultValue) == 1.5 &&
          std::get<bool>(parameterDefinitions[4].defaultValue),
      "LuaScript Params were not parsed from the resource definition");

  auto defaultResource = resourceManager.getResource(
      bw::core::defaultLayerBuildStepScriptName, "World");
  resourceManager.createResource(defaultResource);
  resourceManager.loadResource(defaultResource);
  auto* defaultScript =
      dynamic_cast<bw::core::LuaScriptResource*>(defaultResource.get());
  require(defaultScript && !defaultScript->getText().empty(),
          "the built-in no-op LuaScript was not registered and loadable");

  // Resource refresh is additive: a declaration created after startup becomes
  // selectable without replacing either existing Resource object.
  {
    std::ofstream script(root / "script-late.lua");
    script << "-- added after the initial resource scan\n";
    std::ofstream manifest(root / "Resources.yaml");
    manifest << R"(Resources:
  Namespace:
    name: "World"
    Resource:
      - type: "TextFile"
        name: "ScriptDemoSource"
        location: "script-demo.lua"
      - type: "LuaScript"
        name: "ScriptDimensions"
        location: "script-dimensions.lua"
      - type: "LuaScript"
        name: "ScriptDemo"
        DependentResources:
          DependentResource:
            - id: "Source"
              ref: "ScriptDemoSource"
            - ref: "ScriptDimensions"
      - type: "LuaScript"
        name: "ScriptLate"
        location: "script-late.lua"
)";
  }
  resourceManager.rescanLocations();
  require(resourceManager.getResource("ScriptDemo", "World") == resource &&
              resourceManager.getResourcesByType("LuaScript").size() == 4 &&
              resourceManager.getResource("ScriptLate", "World")->getType() ==
                  "LuaScript",
          "a resource re-scan did not add the new LuaScript additively");

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
    require(script->getText().find("include") != std::string::npos,
            "the root LuaScript resource source was empty or stale");
    script->loadInto(hostRuntime, reference);
  }
  require(hostRuntime.isLoaded("ScriptDemo"),
          "resolved LuaScript text was not compiled into the host runtime");
  require(hostRuntime.getParameterDefinitions("ScriptDemo").size() == 5,
          "the LuaScript resource did not load its Params into the runtime");
  bool reachedContext = false;
  try {
    hostRuntime.execute("ScriptDemo", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const& error) {
    reachedContext = true;
    require(std::string(error.what()).find("attempt to index a nil value") !=
                std::string::npos,
            std::string("the resource-backed include failed before context binding: ") +
                error.what());
  }
  require(reachedContext, "the loaded root LuaScript unexpectedly did nothing");

  bw::core::registerScriptStepTypes(hostRuntime);
  bw::core::Layer probeLayer(0, "probe", 512.0f, 16.0f);
  auto* probeStep = new bw::core::RunScript(hostRuntime);
  probeStep->setScriptName("ScriptDemo");
  probeLayer.addStep(probeStep);
  probeLayer.rebuild();
  require(!probeStep->hasFailed(),
          std::string("resource-backed include probe failed: ") +
              probeStep->getFailureMessage());
  require(probeLayer.getNumPrimitives() == 1 &&
              probeLayer.getPrimitive(0)->getSize() ==
                  wp::Vector2(12.0f, 12.0f),
          "a new RunScript did not use its LuaScript resource defaults");

  bw::core::World defaultWorld(512.0f, 16.0f);
  auto* defaultStep = new bw::core::RunScript(hostRuntime);
  defaultWorld.getActiveLayer()->addStep(defaultStep);
  require(
      defaultStep->getScriptName() ==
              bw::core::defaultLayerBuildStepScriptName &&
          !defaultStep->hasFailed(),
      "a new RunScript did not execute the built-in no-op default");

  bw::core::World loaded(512.0f, 16.0f);
  bool const deserialized = deserializeWorld(yaml, &loaded);
  std::string deserializeError =
      "the host could not deserialize a World after resolving its script";
  for (auto const& error : loaded.getDeserializationErrors()) {
    deserializeError += "\n" + error;
  }
  if (loaded.getActiveLayer()->getNumSteps() > 1) {
    if (auto* failed = dynamic_cast<bw::core::RunScript*>(
            loaded.getActiveLayer()->getStep(1));
        failed && failed->hasFailed()) {
      deserializeError += "\n" + failed->getFailureMessage();
    }
  }
  require(deserialized, deserializeError);
  require(loaded.getActiveLayer()->getNumPrimitives() == 1 &&
              loaded.getActiveLayer()->getPrimitive(0)->getSize() ==
                  wp::Vector2(17.0f, 12.0f),
          "opening the script World did not apply its serialized Param over the resource default");

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
