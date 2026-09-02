// Tests the first end-to-end slice of Lua build scripts at the highest seam
// available: Layer::rebuild(). Each test owns its own ScriptRuntime and
// registers RunScript against it, so nothing here needs a ResourceManager -
// which can only ever be constructed once per process - and every script
// arrives as a plain string.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>

#include <core-lua/CoreLua.h>
#include <core-lua/RunScript.h>
#include <core-lua/ScriptRuntime.h>

namespace {

void require(bool value, char const* message) {
  if (!value) throw std::runtime_error(message);
}

bw::core::RectanglePolygon* rectangle(float x) {
  auto* result = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  result->setSize(8.0f, 8.0f);
  result->setPosition({x, 0.0f});
  return result;
}

bool at(bw::core::Primitive const* primitive, float x) {
  return std::abs(primitive->getPosition().x - x) < .001f;
}

bw::core::RunScript* addScriptStep(
    bw::core::Layer& layer, bw::core::ScriptRuntime& runtime, std::string const& script) {
  auto* step = new bw::core::RunScript(runtime);
  step->setScriptName(script);
  layer.addStep(step);
  return step;
}

void runScriptDeclaresItsCapabilitiesAndIsGivenItsRuntime() {
  bw::core::ScriptRuntime runtime;
  bw::core::registerScriptStepTypes(runtime);

  auto const types = bw::core::LayerBuildStep::getRegisteredTypes();
  require(std::find(types.begin(), types.end(), "RunScript") != types.end(),
          "the step Registry did not enumerate RunScript");

  auto step = std::unique_ptr<bw::core::LayerBuildStep>(
      bw::core::LayerBuildStep::instantiate("RunScript"));
  require(step->getType() == "RunScript" && !step->mayBeFirstStep(),
          "RunScript did not declare its registration");
  require(step->primitivesParticipateInBuild() &&
              !step->permitsDirectPrimitiveEditing() &&
              !step->acceptsNewPrimitives(),
          "RunScript did not declare its build and editing capabilities");
  require(&dynamic_cast<bw::core::RunScript&>(*step).getRuntime() == &runtime,
          "the registration factory did not inject the runtime it was given");
}

void theRuntimeCompilesScriptsFromStringsAndCachesThemByName() {
  bw::core::ScriptRuntime runtime;
  uint32_t calls = 0;
  auto count = [&calls](sol::environment& environment) {
    environment.set_function("record", [&calls]() { ++calls; });
  };

  require(!runtime.isLoaded("counter"), "an unloaded script reported as loaded");
  runtime.load("counter", "record()");
  require(runtime.isLoaded("counter"), "a loaded script did not report as loaded");

  runtime.execute("counter", bw::core::ScriptLibraries::Build, count);
  runtime.execute("counter", bw::core::ScriptLibraries::Build, count);
  require(calls == 2, "the cached chunk did not run once per execution");

  // Loading the same name again replaces the chunk: the reload mechanism.
  runtime.load("counter", "record() record()");
  runtime.execute("counter", bw::core::ScriptLibraries::Build, count);
  require(calls == 4, "loading a script again did not replace its chunk");

  bool reported = false;
  try {
    runtime.execute("absent", bw::core::ScriptLibraries::Build);
  } catch (std::exception const& error) {
    reported = std::string(error.what()).find("absent") != std::string::npos;
  }
  require(reported, "executing a script that was never loaded was not reported");

  reported = false;
  try {
    runtime.load("broken", "this is not Lua");
  } catch (std::exception const& error) {
    reported = std::string(error.what()).find("broken") != std::string::npos;
  }
  require(reported, "a script that does not compile was not reported by name");
}

void theLibrarySetIsAParameterOfExecution() {
  bw::core::ScriptRuntime runtime;
  bool math = false, string = false, table = false;
  auto record = [&](sol::environment& environment) {
    environment.set_function("record", [&](bool m, bool s, bool t) {
      math = m;
      string = s;
      table = t;
    });
  };

  runtime.load("libraries", "record(math ~= nil, string ~= nil, table ~= nil)");

  runtime.execute("libraries", bw::core::ScriptLibraries::Build, record);
  require(math && string && table,
          "the build library set did not offer math, string and table");

  runtime.execute("libraries", bw::core::ScriptLibraries::Base, record);
  require(!math && !string && !table,
          "a narrower library set still offered libraries it did not name");
}

void scriptCreatedPrimitivesFoldInRecipeOrder() {
  bw::core::ScriptRuntime runtime;
  runtime.load("two", R"(
    local first = create_primitive("Rectangle")
    first:set_size(8, 8)
    first:set_position(10, 0)
    place_primitive(first)

    local second = create_primitive("Rectangle")
    second:set_size(8, 8)
    second:set_position(20, 0)
    place_primitive(second)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
  auto* step = addScriptStep(layer, runtime, "two");
  auto const stepIndex = layer.getNumSteps() - 1;
  auto* trailing = new bw::core::PrimitiveField;
  layer.addStep(trailing);
  trailing->addPrimitive(rectangle(30.0f));
  layer.rebuild();

  require(!step->hasFailed(), "rebuilding a Layer did not run its script cleanly");
  require(layer.getNumPrimitives() == 4,
          "the script's Primitives did not appear in the Layer's derived output");
  require(at(layer.getPrimitive(0), 0.0f) && at(layer.getPrimitive(1), 10.0f) &&
              at(layer.getPrimitive(2), 20.0f) && at(layer.getPrimitive(3), 30.0f),
          "the script's Primitives did not fold in recipe order");
  require(layer.getOwningStepIndex(layer.getPrimitive(1)) == stepIndex &&
              step->ownsPrimitive(layer.getPrimitive(1)) &&
              step->ownsPrimitive(layer.getPrimitive(2)),
          "the script's Primitives were not owned by the step that ran it");
  require(!step->ownsPrimitive(layer.getPrimitive(0)),
          "the RunScript step claimed a Primitive another step owns");
}

void everyExecutionRefillsTheStepsOwnStorage() {
  bw::core::ScriptRuntime runtime;
  runtime.load("one", R"(
    local only = create_primitive("Rectangle")
    only:set_position(10, 0)
    place_primitive(only)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "one");

  for (int rebuild = 0; rebuild < 3; ++rebuild) {
    layer.rebuild();
    require(!step->hasFailed() && layer.getNumPrimitives() == 1,
            "a rebuild did not clear and refill the step's storage");
    require(step->ownsPrimitive(layer.getPrimitive(0)) &&
                at(layer.getPrimitive(0), 10.0f),
            "the refilled storage did not hold the script's output");
  }
}

void aScriptNeverOwnsWhatItCreates() {
  bw::core::ScriptRuntime runtime;
  // Two of the three created Primitives are abandoned: one simply never
  // placed, one left behind when the script raises.
  runtime.load("abandon", R"(
    local kept = create_primitive("Rectangle")
    kept:set_position(10, 0)
    place_primitive(kept)

    local dropped = create_primitive("Rectangle")
    dropped:set_position(20, 0)
  )");
  runtime.load("raises", R"(
    local dropped = create_primitive("Rectangle")
    error("the script gave up")
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
  auto* step = addScriptStep(layer, runtime, "abandon");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 2 &&
              at(layer.getPrimitive(1), 10.0f),
          "an abandoned Primitive reached the Layer's derived output");

  step->setScriptName("raises");
  layer.rebuild();

  require(step->hasFailed() &&
              step->getFailureMessage().find("the script gave up") != std::string::npos,
          "a script that raised did not fail its step with what it said");
  require(layer.getNumPrimitives() == 1 && at(layer.getPrimitive(0), 0.0f),
          "a script that raised did not leave the earlier step's output intact");

  step->setScriptName("abandon");
  layer.rebuild();
  require(!step->hasFailed() && layer.getNumPrimitives() == 2,
          "the Layer did not rebuild cleanly after a script raised");
}

void everyExecutionBeginsWithAFreshEnvironment() {
  bw::core::ScriptRuntime runtime;
  runtime.load("leaky", R"(
    if leftover ~= nil then error("a global survived into the next execution") end
    leftover = true

    local only = create_primitive("Rectangle")
    only:set_position(10, 0)
    place_primitive(only)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* first = addScriptStep(layer, runtime, "leaky");
  // A second step running the same script in the same rebuild: neither
  // execution may see what the other left behind.
  auto* second = addScriptStep(layer, runtime, "leaky");

  layer.rebuild();
  require(!first->hasFailed() && !second->hasFailed() && layer.getNumPrimitives() == 2,
          "two steps running one script observed each other's globals");

  layer.rebuild();
  require(!first->hasFailed() && !second->hasFailed() && layer.getNumPrimitives() == 2,
          "a global set during one rebuild survived into the next");
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    runScriptDeclaresItsCapabilitiesAndIsGivenItsRuntime();
    theRuntimeCompilesScriptsFromStringsAndCachesThemByName();
    theLibrarySetIsAParameterOfExecution();
    scriptCreatedPrimitivesFoldInRecipeOrder();
    everyExecutionRefillsTheStepsOwnStorage();
    aScriptNeverOwnsWhatItCreates();
    everyExecutionBeginsWithAFreshEnvironment();
    std::cout << "RunScript build step and ScriptRuntime tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
