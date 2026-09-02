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
#include <vector>

#include <willpower/common/BoundingBox.h>

#include <core/DefinePrefabs.h>
#include <core/Layer.h>
#include <core/LayerBuildStep.h>
#include <core/PrimitiveField.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

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

std::string serializeWorld(bw::core::World const& world) {
  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  world.serialize(writer, workData);
  return writer->getSerializedString();
}

bool deserializeWorld(std::string const& yaml, bw::core::World* world) {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  reader->deserialize();
  bw::core::SerializationWorkData workData{16.0f};
  return world->deserialize(reader, workData);
}

bw::core::RunScript* addScriptStep(
    bw::core::Layer& layer, bw::core::ScriptRuntime& runtime, std::string const& script) {
  auto* step = new bw::core::RunScript(runtime);
  step->setScriptName(script);
  layer.addStep(step);
  return step;
}

// Adds a DefinePrefabs step named stepName, holding one Prefab named
// prefabName with a single Primitive at prefabLocalX (positioned in the
// Prefab's own local space, before any instance transform).
bw::core::DefinePrefabs* addPrefabDefinitions(
    bw::core::Layer& layer, std::string const& stepName, std::string const& prefabName,
    float prefabLocalX) {
  auto* definitions = new bw::core::DefinePrefabs;
  definitions->setName(stepName);
  auto const defineIndex = layer.addStep(definitions);
  auto* prefab = definitions->addPrefab(prefabName);
  definitions->setSelectedPrefab(prefab);
  layer.setActiveStep(defineIndex);
  layer.addPrimitive(rectangle(prefabLocalX));
  definitions->clearSelectedPrefab();
  layer.setActiveStep(0);
  return definitions;
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

  runtime.load("broken", "return true");
  reported = false;
  try {
    runtime.load("broken", "this is not Lua");
  } catch (std::exception const& error) {
    reported = std::string(error.what()).find("broken") != std::string::npos;
  }
  require(reported, "a script that does not compile was not reported by name");

  // The broken text replaces any prior chunk and remains executable as a
  // failure, so a RunScript naming it can report the syntax error on rebuild.
  require(!runtime.isLoaded("broken"), "broken text was reported as a compiled script");
  reported = false;
  try {
    runtime.execute("broken", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const& error) {
    reported = error.getLineNumber() == 1 &&
               std::string(error.what()).find("failed to compile") != std::string::npos;
  }
  require(reported, "executing retained broken text did not report its syntax line");
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

// Ticket #367: every ordinary way a script can be broken is contained by
// Layer::rebuild(), reports useful source information, rolls back partial
// output, and prevents every later step from running.
void syntaxRuntimeAndBudgetFailuresAreContainedAndHaltTheBuild() {
  bw::core::ScriptRuntime runtime;

  try {
    runtime.load("syntax", "local okay = true\nlocal broken = )");
  } catch (bw::core::ScriptException const&) {
    // load() reports immediately to the resource host as well as retaining
    // the error for the step's next rebuild.
  }

  runtime.load("runtime", R"(local p = create_primitive("Rectangle")
p:set_position(10, 0)
place_primitive(p)
local function explode()
  error("deliberate runtime failure")
end
explode())");

  runtime.load("runaway", R"(local p = create_primitive("Rectangle")
p:set_position(20, 0)
place_primitive(p)
while true do
  pcall(function() while true do end end)
end)");

  struct ObservedFailure {
    uint32_t lineNumber;
    std::string message;
    std::string traceback;
  };

  auto exerciseFailure = [&](std::string const& scriptName) {
    bw::core::Layer layer(0, "test", 512.0f, 16.0f);
    layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
    auto* failed = addScriptStep(layer, runtime, scriptName);
    auto* later = new bw::core::PrimitiveField;
    layer.addStep(later);
    later->addPrimitive(rectangle(30.0f));

    // Explicitly exercise the public seam: none of these may escape.
    layer.rebuild();

    require(failed->hasFailed(), "a broken script did not fail its RunScript step");
    require(!failed->getFailureMessage().empty(),
            "a broken script did not retain a failure message");
    require(layer.getNumPrimitives() == 1 && at(layer.getPrimitive(0), 0.0f),
            "a failed script kept partial output or allowed a later step to run");
    require(!later->hasFailed(),
            "a step halted below a broken script was itself marked as failed");
    return ObservedFailure{
        failed->getFailureLineNumber(), failed->getFailureMessage(),
        failed->getFailureTraceback()};
  };

  auto const syntax = exerciseFailure("syntax");
  require(syntax.lineNumber == 2,
          "a syntax failure did not retain its source line");

  auto const runtimeFailure = exerciseFailure("runtime");
  require(runtimeFailure.lineNumber == 5,
          "a runtime failure did not retain its source line");
  require(runtimeFailure.message.find("deliberate runtime failure") !=
              std::string::npos,
          "a runtime failure did not retain its message");
  require(runtimeFailure.traceback.find("stack traceback") !=
              std::string::npos,
          "a runtime failure did not retain its Lua traceback");

  auto const runaway = exerciseFailure("runaway");
  require(runaway.message.find("instruction budget") != std::string::npos,
          "a non-terminating script was not stopped by the instruction budget");
  require(runaway.lineNumber != 0 &&
              runaway.traceback.find("stack traceback") != std::string::npos,
          "an instruction-budget failure did not retain its line and traceback");

  runtime.load("empty", "");
  bw::core::Layer emptyLayer(0, "test", 512.0f, 16.0f);
  auto* empty = addScriptStep(emptyLayer, runtime, "empty");
  emptyLayer.rebuild();
  require(!empty->hasFailed() && emptyLayer.getNumPrimitives() == 0,
          "a script that successfully produced nothing looked like a failed script");
}

void theBuildEnvironmentDropsFunctionsThatBreakDeterminism() {
  bw::core::ScriptRuntime runtime;
  bool loadAbsent = false, loadfileAbsent = false, dofileAbsent = false,
       collectgarbageAbsent = false, ioAbsent = false, osAbsent = false,
       debugAbsent = false, packageAbsent = false, requireAbsent = false;
  auto record = [&](sol::environment& environment) {
    environment.set_function(
        "record",
        [&](bool load, bool loadfile, bool dofile, bool collectgarbage, bool io,
            bool os, bool debug, bool package, bool require) {
          loadAbsent = !load;
          loadfileAbsent = !loadfile;
          dofileAbsent = !dofile;
          collectgarbageAbsent = !collectgarbage;
          ioAbsent = !io;
          osAbsent = !os;
          debugAbsent = !debug;
          packageAbsent = !package;
          requireAbsent = !require;
        });
  };

  runtime.load("restricted", R"(
    record(load ~= nil, loadfile ~= nil, dofile ~= nil, collectgarbage ~= nil,
           io ~= nil, os ~= nil, debug ~= nil, package ~= nil, require ~= nil)
  )");

  runtime.execute("restricted", bw::core::ScriptLibraries::Build, record);
  require(loadAbsent && loadfileAbsent && dofileAbsent && collectgarbageAbsent &&
              ioAbsent && osAbsent && debugAbsent && packageAbsent && requireAbsent,
          "the build environment offered a function that breaks determinism");
}

void printReachesTheHostsSink() {
  std::vector<std::string> lines;
  bw::core::ScriptRuntime runtime(
      [&lines](std::string const& line) { lines.push_back(line); });
  runtime.load("greet", R"(print("hello", 1, true))");

  runtime.execute("greet", bw::core::ScriptLibraries::Build);

  require(lines.size() == 1 && lines[0] == "hello\t1\ttrue",
          "print did not reach the host's sink with Lua's own joining");
}

void theSeedMakesRebuildsReproducibleAndRerollableByChangingIt() {
  bw::core::ScriptRuntime runtime;
  runtime.load("scatter", R"(
    local p = create_primitive("Rectangle")
    p:set_position(math.random(0, 1000), 0)
    place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "scatter");
  step->setSeed(7);

  layer.rebuild();
  float const firstX = layer.getPrimitive(0)->getPosition().x;
  layer.rebuild();
  float const secondX = layer.getPrimitive(0)->getPosition().x;
  require(firstX == secondX, "rebuilding the same Layer twice did not reproduce the seed's output");

  step->setSeed(8);
  layer.rebuild();
  float const rerolledX = layer.getPrimitive(0)->getPosition().x;
  require(rerolledX != firstX, "changing the seed did not change the output");

  step->setSeed(7);
  layer.rebuild();
  float const restoredX = layer.getPrimitive(0)->getPosition().x;
  require(restoredX == firstX, "restoring the seed did not restore the previous output");
}

void twoRunScriptStepsCannotObserveEachOthersRandomState() {
  bw::core::ScriptRuntime runtime;
  runtime.load("draw", R"(
    local p = create_primitive("Rectangle")
    p:set_position(math.random(0, 1000), 0)
    place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* first = addScriptStep(layer, runtime, "draw");
  first->setSeed(42);
  auto* second = addScriptStep(layer, runtime, "draw");
  second->setSeed(42);

  layer.rebuild();
  require(layer.getPrimitive(0)->getPosition().x == layer.getPrimitive(1)->getPosition().x,
          "two steps with the same seed running the same script produced different output "
          "because one observed the other's leftover random state");
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

void scriptsReadPriorBuildPrimitivesAsConstHandles() {
  bw::core::ScriptRuntime runtime;
  runtime.load("count", R"(
    local priors = get_build_primitives()
    local p = create_primitive("Rectangle")
    p:set_size(4, 4)
    p:set_position(#priors * 10, 0)
    place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
  layer.getPrimitiveField()->addPrimitive(rectangle(100.0f));
  auto* step = addScriptStep(layer, runtime, "count");
  layer.rebuild();

  require(!step->hasFailed(), "reading prior build Primitives failed the step");
  require(layer.getNumPrimitives() == 3 && at(layer.getPrimitive(2), 20.0f),
          "a script did not see the build Primitives produced by preceding steps");
}

void aScriptCannotMutateAPriorPrimitive() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mutate", R"(
    local priors = get_build_primitives()
    priors[1]:set_position(999, 999)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
  auto* step = addScriptStep(layer, runtime, "mutate");
  layer.rebuild();

  require(step->hasFailed(), "a script mutating a prior Primitive's const handle was not rejected");
  require(at(layer.getPrimitive(0), 0.0f),
          "a prior Primitive was mutated through a handle the script must not be able to write through");
}

void aScriptReadsTheLayersExtents() {
  bw::core::ScriptRuntime runtime;
  runtime.load("extents", R"(
    local x, y, w, h = get_extents()
    local p = create_primitive("Rectangle")
    p:set_size(4, 4)
    p:set_position(x + w, y + h)
    place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.setExtents(wp::BoundingBox(-64.0f, -32.0f, 128.0f, 64.0f));
  addScriptStep(layer, runtime, "extents");
  layer.rebuild();

  require(layer.getNumPrimitives() == 1 && at(layer.getPrimitive(0), 64.0f),
          "a script did not read the Layer's extents through the context");
}

void aScatterAvoidsExistingGeometryAndItsOwnPlacements() {
  // A Primitive's default (non-exact) bounds pad every side by the orbit
  // animator's default distance (100), regardless of its authored size, so
  // candidates need to sit further apart than that padding for the area
  // query to distinguish neighbouring slots. A query box matching each
  // candidate's own slot (corner-anchored per wp::BoundingBox) is enough
  // margin for one slot's padded bounds not to reach the next.
  bw::core::ScriptRuntime runtime;
  runtime.load("scatter", R"(
    local spacing = 250
    for i = 1, 6 do
      local cx = (i - 1) * spacing
      if #find_build_primitives_overlapping(cx - spacing / 2, -spacing / 2, spacing, spacing) == 0 then
        local p = create_primitive("Rectangle")
        p:set_size(8, 8)
        p:set_position(cx, 0)
        place_primitive(p)
      end
    end
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  layer.getPrimitiveField()->addPrimitive(rectangle(0.0f));
  auto* step = addScriptStep(layer, runtime, "scatter");
  layer.rebuild();

  require(!step->hasFailed(), "the scatter script failed to run");
  // Slot 0 (cx=0) is occupied by the pre-existing rectangle, so exactly 5
  // new rectangles are placed alongside the one that was already there.
  require(layer.getNumPrimitives() == 6,
          "a scatter using the area query did not avoid existing geometry and its own placements");

  for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) {
    for (uint32_t j = i + 1; j < layer.getNumPrimitives(); ++j) {
      require(!layer.getPrimitive(i)->getBounds().intersectsBoundingObject(&layer.getPrimitive(j)->getBounds()),
              "the scatter produced overlapping Primitives");
    }
  }

  auto const firstRunPositions = [&] {
    std::vector<float> xs;
    for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) xs.push_back(layer.getPrimitive(i)->getPosition().x);
    return xs;
  }();

  layer.rebuild();
  require(layer.getNumPrimitives() == firstRunPositions.size(),
          "the same scatter did not reproduce the same count across rebuilds");
  for (uint32_t i = 0; i < layer.getNumPrimitives(); ++i) {
    require(at(layer.getPrimitive(i), firstRunPositions[i]),
            "the same scatter did not reproduce exactly across rebuilds");
  }
}

void aScriptPlacesTransformedPrefabInstancesFoundByName() {
  bw::core::ScriptRuntime runtime;
  runtime.load("stamp", R"(
    local prefabs = find_define_prefabs("prefabs")
    local rock = prefabs:get_prefab("rock")
    place_prefab_instance(rock, 100, 0, 0)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  addPrefabDefinitions(layer, "prefabs", "rock", 3.0f);
  auto* step = addScriptStep(layer, runtime, "stamp");
  layer.rebuild();

  require(!step->hasFailed(), "placing a Prefab instance found by name failed the step");
  require(layer.getNumPrimitives() == 1, "the placed Prefab instance did not reach the Layer");
  require(at(layer.getPrimitive(0), 103.0f),
          "the placed instance was not offset by the position the script gave it");
  require(step->ownsPrimitive(layer.getPrimitive(0)),
          "the RunScript step did not own the Primitive it placed as a Prefab instance");
}

void placedPrefabInstancesAreCopiesLeavingThePrefabUnchanged() {
  bw::core::ScriptRuntime runtime;
  runtime.load("stamp-twice", R"(
    local prefabs = find_define_prefabs("prefabs")
    local rock = prefabs:get_prefab("rock")
    place_prefab_instance(rock, 100, 0, 0)
    place_prefab_instance(rock, 200, 0, 0)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = addPrefabDefinitions(layer, "prefabs", "rock", 3.0f);
  auto* step = addScriptStep(layer, runtime, "stamp-twice");
  layer.rebuild();

  require(!step->hasFailed(), "placing two Prefab instances failed the step");
  require(layer.getNumPrimitives() == 2, "both placed instances did not reach the Layer");
  require(at(layer.getPrimitive(0), 103.0f) && at(layer.getPrimitive(1), 203.0f),
          "placed instances were not independently positioned copies");
  require(layer.getPrimitive(0) != layer.getPrimitive(1),
          "two placed instances shared the same Primitive rather than each being a copy");

  auto* sourcePrimitive = definitions->getPrefab(0)->getPrimitive(0);
  require(std::abs(sourcePrimitive->getPosition().x - 3.0f) < .001f,
          "placing transformed instances moved the Prefab's own Primitive");
}

void aScriptReadsAPrimitiveFieldsPrimitivesAsConst() {
  bw::core::ScriptRuntime runtime;
  runtime.load("read-field", R"(
    local field = find_primitive_field("authored")
    local primitives = field:get_primitives()
    local p = create_primitive("Rectangle")
    p:set_position(#primitives * 10, 0)
    place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* authored = new bw::core::PrimitiveField;
  authored->setName("authored");
  layer.addStep(authored);
  authored->addPrimitive(rectangle(0.0f));
  authored->addPrimitive(rectangle(50.0f));
  auto* step = addScriptStep(layer, runtime, "read-field");
  layer.rebuild();

  require(!step->hasFailed(), "reading a PrimitiveField's Primitives by name failed the step");
  require(layer.getNumPrimitives() == 3 && at(layer.getPrimitive(2), 20.0f),
          "a script did not read a PrimitiveField's Primitives found by name");
}

void aScriptCannotMutateAPrimitiveFieldsPrimitiveReadByName() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mutate-field", R"(
    local field = find_primitive_field("authored")
    local primitives = field:get_primitives()
    primitives[1]:set_position(999, 999)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* authored = new bw::core::PrimitiveField;
  authored->setName("authored");
  layer.addStep(authored);
  authored->addPrimitive(rectangle(0.0f));
  auto* step = addScriptStep(layer, runtime, "mutate-field");
  layer.rebuild();

  require(step->hasFailed(),
          "a script mutating a PrimitiveField Primitive's const handle was not rejected");
}

// Ticket #368: the public World serialization seam carries all authored
// RunScript state and its exact dependency projection, then rebuilding the
// loaded recipe reproduces the script's output.
void aWorldRoundTripsARunScriptStepAndDeclaresItsResources() {
  bw::core::ScriptRuntime runtime;
  bw::core::registerScriptStepTypes(runtime);
  runtime.load("Scripts/scatter", R"(
    local p = create_primitive("Rectangle")
    p:set_size(8, 8)
    p:set_position(math.random(100, 1000), 0)
    place_primitive(p)
  )");
  runtime.load("Scripts/disabled", "");

  bw::core::World source(512.0f, 16.0f);
  auto* sourceLayer = source.getActiveLayer();
  sourceLayer->getPrimitiveField()->addPrimitive(rectangle(0.0f));

  auto* sourceStep = new bw::core::RunScript(runtime);
  sourceStep->setScriptName("Scripts/scatter");
  sourceStep->setSeed(0x123456789abcdef0ull);
  sourceStep->setName("seeded scatter");
  sourceStep->setExtraResourceNames(
      {"Materials/stone", "Images/wall", "Materials/stone", "Scripts/scatter"});
  auto const sourceStepIndex = sourceLayer->addStep(sourceStep);

  auto* disabledStep = new bw::core::RunScript(runtime);
  disabledStep->setScriptName("Scripts/disabled");
  disabledStep->setName("disabled script");
  auto const disabledStepIndex = sourceLayer->addStep(disabledStep);
  sourceLayer->setStepEnabled(disabledStepIndex, false);

  auto const yaml = serializeWorld(source);
  auto dependencyReader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  dependencyReader->deserialize();
  require(
      bw::core::World::readDependentResourceNames(dependencyReader) ==
          std::vector<std::string>{"Images/wall", "Materials/stone", "Scripts/disabled", "Scripts/scatter"},
      "RunScript steps' serialized dependent resources were not exactly sorted and unique");

  bw::core::World loaded(512.0f, 16.0f);
  require(deserializeWorld(yaml, &loaded),
          "a World containing a RunScript step did not deserialize");
  auto* loadedLayer = loaded.getActiveLayer();
  auto* loadedStep = dynamic_cast<bw::core::RunScript*>(
      loadedLayer->getStep(sourceStepIndex));
  require(loadedStep, "the loaded recipe did not restore its RunScript step type");
  require(loadedStep->getScriptName() == "Scripts/scatter" &&
              loadedStep->getSeed() == 0x123456789abcdef0ull &&
              loadedStep->getExtraResourceNames() ==
                  std::vector<std::string>{"Materials/stone", "Images/wall", "Materials/stone", "Scripts/scatter"} &&
              loadedStep->getName() == "seeded scatter" &&
              loadedStep->isEnabled(),
          "a RunScript step lost authored state during its World round-trip");
  require(!loadedLayer->getStep(disabledStepIndex)->isEnabled(),
          "a disabled RunScript step was enabled by its World round-trip");
  require(sourceLayer->getNumPrimitives() == 2 &&
              loadedLayer->getNumPrimitives() == 2 &&
              sourceLayer->getPrimitive(1)->getPosition() ==
                  loadedLayer->getPrimitive(1)->getPosition(),
          "the loaded RunScript recipe did not rebuild the same Primitives");
}

void reloadingRebuildsExactlyTheLayersThatNameTheScript() {
  std::vector<std::string> printed;
  bw::core::ScriptRuntime runtime(
      [&printed](std::string const& line) { printed.push_back(line); });

  auto sourceAt = [](float x, std::string const& message) {
    return "print(\"" + message + "\")\n"
           "local p = create_primitive(\"Rectangle\")\n"
           "p:set_position(" + std::to_string(x) + ", 0)\n"
           "place_primitive(p)";
  };

  runtime.load("shared", sourceAt(10, "shared-old"));
  runtime.load("other", sourceAt(30, "other"));

  bw::core::Layer first(0, "first", 512.0f, 16.0f);
  bw::core::Layer second(1, "second", 512.0f, 16.0f);
  bw::core::Layer unrelated(2, "unrelated", 512.0f, 16.0f);
  auto* firstStep = addScriptStep(first, runtime, "shared");
  auto* duplicateStep = addScriptStep(first, runtime, "shared");
  auto* secondStep = addScriptStep(second, runtime, "shared");
  addScriptStep(unrelated, runtime, "other");
  printed.clear();

  runtime.reload("shared", sourceAt(20, "shared-new"));
  require(first.getNumPrimitives() == 2 && at(first.getPrimitive(0), 20) &&
              at(first.getPrimitive(1), 20) &&
              second.getNumPrimitives() == 1 && at(second.getPrimitive(0), 20),
          "reload did not replace the cached chunk in every naming Layer");
  require(unrelated.getNumPrimitives() == 1 && at(unrelated.getPrimitive(0), 30),
          "reload changed a Layer that does not name the script");
  require(std::count(printed.begin(), printed.end(), "shared-new") == 3 &&
              std::count(printed.begin(), printed.end(), "other") == 0,
          "reload did not rebuild exactly the naming Layers once each");

  // A step's optional label is unrelated to its script reference and must not
  // disturb the lookup. Removing one of two matching steps must retain the
  // Layer through the other; repointing the last matching step removes it.
  firstStep->setName("renamed step");
  secondStep->setScriptName("other");
  first.removeStep(1);
  printed.clear();
  runtime.reload("shared", sourceAt(40, "shared-after-removal"));
  require(std::count(printed.begin(), printed.end(), "shared-after-removal") == 1,
          "removing one matching step also removed its Layer's other reverse entry");
  duplicateStep->setScriptName("other");
  printed.clear();
  runtime.reload("shared", sourceAt(50, "shared-after-repoint"));
  require(printed.empty(),
          "a renamed, repointed, or removed step left a stale reverse entry");

  // Adding another naming step establishes a fresh entry. Broken replacement
  // text is retained as one coherent failure and its Layer is rebuilt, while
  // the unrelated Layer is still untouched.
  auto* added = addScriptStep(first, runtime, "shared");
  printed.clear();
  bool reported = false;
  try {
    runtime.reload("shared", "this is not Lua");
  } catch (bw::core::ScriptException const& error) {
    reported = std::string(error.what()).find("shared") != std::string::npos;
  }
  require(reported && added->hasFailed() && first.getNumPrimitives() == 1 &&
              at(first.getPrimitive(0), 30),
          "a failed reload was not reported through the rebuilt naming step");
  require(std::count(printed.begin(), printed.end(), "other") == 1,
          "a failed reload rebuilt the unrelated Layer using another script");
  require(!runtime.isLoaded("shared"),
          "a failed reload left a half-updated compiled chunk in the cache");
}

void namingAMissingStepOrPrefabFailsTheStepWithTheName() {
  bw::core::ScriptRuntime runtime;
  runtime.load("missing-step", R"(find_define_prefabs("nope"))");
  runtime.load("missing-field", R"(find_primitive_field("nope"))");
  runtime.load("wrong-type", R"(find_define_prefabs("authored"))");
  runtime.load("missing-prefab", R"(
    local prefabs = find_define_prefabs("prefabs")
    prefabs:get_prefab("nope")
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  addPrefabDefinitions(layer, "prefabs", "rock", 3.0f);
  auto* authored = new bw::core::PrimitiveField;
  authored->setName("authored");
  layer.addStep(authored);

  auto* step = addScriptStep(layer, runtime, "missing-step");
  layer.rebuild();
  require(step->hasFailed() && step->getFailureMessage().find("nope") != std::string::npos,
          "naming a missing step did not fail with a message identifying it");

  step->setScriptName("missing-field");
  layer.rebuild();
  require(step->hasFailed() && step->getFailureMessage().find("nope") != std::string::npos,
          "naming a missing PrimitiveField step did not fail with a message identifying it");

  step->setScriptName("wrong-type");
  layer.rebuild();
  require(step->hasFailed() && step->getFailureMessage().find("authored") != std::string::npos,
          "naming a step of the wrong type did not fail with a message identifying it");

  step->setScriptName("missing-prefab");
  layer.rebuild();
  require(step->hasFailed() && step->getFailureMessage().find("nope") != std::string::npos,
          "naming a missing Prefab did not fail with a message identifying it");
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
    syntaxRuntimeAndBudgetFailuresAreContainedAndHaltTheBuild();
    theBuildEnvironmentDropsFunctionsThatBreakDeterminism();
    printReachesTheHostsSink();
    theSeedMakesRebuildsReproducibleAndRerollableByChangingIt();
    twoRunScriptStepsCannotObserveEachOthersRandomState();
    everyExecutionBeginsWithAFreshEnvironment();
    scriptsReadPriorBuildPrimitivesAsConstHandles();
    aScriptCannotMutateAPriorPrimitive();
    aScriptReadsTheLayersExtents();
    aScatterAvoidsExistingGeometryAndItsOwnPlacements();
    aScriptPlacesTransformedPrefabInstancesFoundByName();
    placedPrefabInstancesAreCopiesLeavingThePrefabUnchanged();
    aScriptReadsAPrimitiveFieldsPrimitivesAsConst();
    aScriptCannotMutateAPrimitiveFieldsPrimitiveReadByName();
    aWorldRoundTripsARunScriptStepAndDeclaresItsResources();
    reloadingRebuildsExactlyTheLayersThatNameTheScript();
    namingAMissingStepOrPrefabFailsTheStepWithTheName();
    std::cout << "RunScript build step and ScriptRuntime tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
