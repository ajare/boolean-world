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
    theBuildEnvironmentDropsFunctionsThatBreakDeterminism();
    printReachesTheHostsSink();
    theSeedMakesRebuildsReproducibleAndRerollableByChangingIt();
    twoRunScriptStepsCannotObserveEachOthersRandomState();
    everyExecutionBeginsWithAFreshEnvironment();
    scriptsReadPriorBuildPrimitivesAsConstHandles();
    aScriptCannotMutateAPriorPrimitive();
    aScriptReadsTheLayersExtents();
    aScatterAvoidsExistingGeometryAndItsOwnPlacements();
    std::cout << "RunScript build step and ScriptRuntime tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
