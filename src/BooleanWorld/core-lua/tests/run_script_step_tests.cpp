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
#include <core/MeshPrimitive.h>
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

std::vector<bw::core::ScriptParameterDefinition> scriptParameterDefinitions() {
  using bw::core::ScriptParameterDefinition;
  using bw::core::ScriptParameterType;

  ScriptParameterDefinition label;
  label.name = "label";
  label.type = ScriptParameterType::String;
  label.defaultValue = std::string("mine");

  ScriptParameterDefinition style;
  style.name = "style";
  style.type = ScriptParameterType::String;
  style.defaultValue = std::string("rough");
  style.choices = {"rough", "smooth"};

  ScriptParameterDefinition count;
  count.name = "count";
  count.type = ScriptParameterType::Integer;
  count.defaultValue = int64_t{4};
  count.integerMinimum = 1;
  count.integerMaximum = 20;

  ScriptParameterDefinition scale;
  scale.name = "scale";
  scale.type = ScriptParameterType::Number;
  scale.defaultValue = 1.5;
  scale.numberMinimum = 0.25;
  scale.numberMaximum = 4.0;

  ScriptParameterDefinition enabled;
  enabled.name = "enabled";
  enabled.type = ScriptParameterType::Boolean;
  enabled.defaultValue = true;

  return {label, style, count, scale, enabled};
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

void resourceParametersAreExposedThroughTheParamsTable() {
  bw::core::ScriptRuntime runtime;
  runtime.load("parameterized", R"(
    assert(type(params) == "table")
    assert(type(params.label) == "string")
    assert(math.type(params.count) == "integer")
    assert(type(params.scale) == "number")
    assert(type(params.enabled) == "boolean")
    local primitive = context:create_primitive("Rectangle")
    primitive:set_position(params.count, params.scale)
    primitive:set_size(params.enabled and 8 or 4, #params.label)
    primitive:set_priority(params.style == "smooth" and 2 or 1)
    context:place_primitive(primitive)
  )",
               {}, scriptParameterDefinitions());

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "parameterized");
  require(!step->hasFailed() && layer.getNumPrimitives() == 1 &&
              layer.getPrimitive(0)->getPosition() == wp::Vector2(4.0f, 1.5f) &&
              layer.getPrimitive(0)->getSize() == wp::Vector2(8.0f, 4.0f) &&
              layer.getPrimitive(0)->getPriority() == 1,
          "a new RunScript did not execute with its resource defaults");

  step->setParameterValue("label", std::string("tunnels"));
  step->setParameterValue("style", std::string("smooth"));
  step->setParameterValue("count", int64_t{12});
  step->setParameterValue("scale", 2.25);
  step->setParameterValue("enabled", false);
  layer.rebuild();
  require(!step->hasFailed() &&
              layer.getPrimitive(0)->getPosition() == wp::Vector2(12.0f, 2.25f) &&
              layer.getPrimitive(0)->getSize() == wp::Vector2(4.0f, 7.0f) &&
              layer.getPrimitive(0)->getPriority() == 2,
          "serialized RunScript choices did not override resource defaults");

  step->clearParameterValue("count");
  layer.rebuild();
  require(layer.getPrimitive(0)->getPosition().x == 4.0f,
          "clearing a RunScript choice did not restore the resource default");

  bool rejected = false;
  try {
    step->setParameterValue("count", int64_t{21});
  } catch (bw::core::CoreException const&) {
    rejected = true;
  }
  require(rejected, "RunScript accepted a parameter outside its resource range");
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

void runScriptOperationsAreScopedToTheExecutionContext() {
  bw::core::ScriptRuntime runtime;
  runtime.load("context", R"(
    assert(context ~= nil)
    assert(create_primitive == nil)
    assert(create_mesh_primitive == nil)
    assert(place_primitive == nil)
    assert(place_prefab_instance == nil)
    assert(get_tile == nil)
    assert(find_define_prefabs == nil)
    assert(find_primitive_field == nil)
    assert(get_build_primitives == nil)
    assert(get_extents == nil)
    assert(find_build_primitives_overlapping == nil)

    local primitive = context:create_primitive("Rectangle")
    context:place_primitive(primitive)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "context");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "RunScript operations were not scoped to the execution context");
  require(!layer.getPrimitive(0)->getVertices().empty(),
          "a script-created Rectangle had no renderable geometry");
}

void aScriptCreatesAMeshPrimitiveFromOneRing() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mesh", R"(
    local mesh = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    assert(mesh:get_type() == "Mesh")
    context:place_primitive(mesh)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "mesh");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "creating and placing a MeshPrimitive from a Lua Ring failed");
  auto* mesh = dynamic_cast<bw::core::MeshPrimitive*>(layer.getPrimitive(0));
  require(mesh != nullptr, "create_mesh_primitive did not create a MeshPrimitive");
  auto proxy = mesh->createEditingProxy();
  uint32_t vertexCount = 0;
  for (auto id = proxy->getFirstVertexIndex();
       !proxy->vertexIndexIterationFinished(id);
       id = proxy->getNextVertexIndex(id)) {
    ++vertexCount;
  }
  require(vertexCount == 4,
          "create_mesh_primitive did not preserve the Ring's four vertices");
  wp::Vector2 minimum, maximum;
  proxy->getExtents(minimum, maximum);
  require(minimum == wp::Vector2{0.0f, 0.0f} &&
              maximum == wp::Vector2{8.0f, 8.0f},
          "create_mesh_primitive did not interpret Ring points in the World plane");
}

void meshGeometryOperationsKeepIdsWithinOneExecution() {
  bw::core::ScriptRuntime runtime;
  runtime.load("edit-mesh", R"(
    local mesh = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    local vertex_id = mesh:split_edge(0)
    assert(vertex_id ~= nil)
    assert(mesh:move_vertex_to(vertex_id, 4, -2))
    context:place_primitive(mesh)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "edit-mesh");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "editing a MeshPrimitive by sub-object id failed");
  auto* mesh = dynamic_cast<bw::core::MeshPrimitive*>(layer.getPrimitive(0));
  auto proxy = mesh->createEditingProxy();
  bool foundMovedSplit = false;
  for (auto id = proxy->getFirstVertexIndex();
       !proxy->vertexIndexIterationFinished(id);
       id = proxy->getNextVertexIndex(id)) {
    foundMovedSplit |= proxy->getVertex(id).getPosition() == wp::Vector2{4.0f, -2.0f};
  }
  require(foundMovedSplit,
          "a split Vertex id did not remain usable by the next Lua geometry operation");
}

void scriptsMoveAndRemoveMeshSubObjectsById() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mesh-sub-objects", R"(
    local moved = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    assert(moved:move_vertex(1, 0, -1))
    assert(moved:move_edge(2, 1, 0))
    assert(moved:move_polygon(0, 10, 0))
    context:place_primitive(moved)

    local vertex_removed = context:create_mesh_primitive({
      {20, 0}, {24, -1}, {28, 0}, {28, 8}, {20, 8}
    })
    assert(vertex_removed:remove_vertex(1))
    context:place_primitive(vertex_removed)

    local edge_removed = context:create_mesh_primitive({
      {40, 0}, {44, -1}, {48, 0}, {48, 8}, {40, 8}
    })
    assert(edge_removed:remove_edge(0))
    context:place_primitive(edge_removed)

    local polygon_removed = context:create_mesh_primitive({
      {60, 0}, {68, 0}, {68, 8}, {60, 8}
    })
    local added = polygon_removed:add_shell({
      {72, 0}, {80, 0}, {80, 8}, {72, 8}
    })
    assert(added ~= nil and polygon_removed:remove_polygon(added))
    context:place_primitive(polygon_removed)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "mesh-sub-objects");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 4,
          "moving or removing a Mesh sub-object by id failed");
}

void scriptsAuthorAndSliceMeshContainmentByPolygonId() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mesh-containment", R"(
    local nested = context:create_mesh_primitive({
      {0, 0}, {20, 0}, {20, 20}, {0, 20}
    })
    local hole = nested:add_hole(0, {
      {4, 4}, {16, 4}, {16, 16}, {4, 16}
    })
    assert(hole ~= nil)
    local island = nested:add_island(hole, {
      {7, 7}, {13, 7}, {13, 13}, {7, 13}
    })
    assert(island ~= nil)
    assert(nested:fill_hole(hole) ~= nil)
    context:place_primitive(nested)

    local sliced = context:create_mesh_primitive({
      {30, 0}, {40, 0}, {40, 10}, {30, 10}
    })
    assert(sliced:slice_polygon(0, 0, 2))
    context:place_primitive(sliced)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "mesh-containment");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 2,
          "authoring Mesh containment or slicing by polygon id failed");
  auto* sliced = dynamic_cast<bw::core::MeshPrimitive*>(layer.getPrimitive(1));
  require(sliced && sliced->getShells().size() == 2,
          "slice_polygon did not divide one Shell into two Shells");
}

void meshPrimitivesRetainTheMutablePrimitiveApi() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mesh-primitive-api", R"(
    local mesh = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    mesh:set_position(20, 30)
    mesh:set_priority(17)
    mesh:set_operation("difference")
    local x, y = mesh:get_position()
    assert(x == 20 and y == 30)
    assert(mesh:get_priority() == 17)
    assert(mesh:get_operation() == "difference")
    context:place_primitive(mesh)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "mesh-primitive-api");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "a MeshPrimitive did not retain the mutable Primitive API");
  auto* mesh = dynamic_cast<bw::core::MeshPrimitive*>(layer.getPrimitive(0));
  require(mesh && mesh->getPosition() == wp::Vector2{20.0f, 30.0f} &&
              mesh->getPriority() == 17 &&
              mesh->getOperation() == bw::core::Primitive::Operation::Difference,
          "MeshPrimitive common properties did not cross the Lua API");
}

void scriptsAuthorIndependentElevationPlanes() {
  bw::core::ScriptRuntime runtime;
  runtime.load("elevation-planes", R"(
    local primitive = context:create_primitive("Rectangle")
    primitive:set_floor_elevation(-4, 0.25, -0.5)
    primitive:set_ceiling_elevation(60, -0.125, 0.75)
    local floor_base, floor_x, floor_y = primitive:get_floor_elevation()
    local ceiling_base, ceiling_x, ceiling_y = primitive:get_ceiling_elevation()
    assert(floor_base == -4 and floor_x == 0.25 and floor_y == -0.5)
    assert(ceiling_base == 60 and ceiling_x == -0.125 and ceiling_y == 0.75)
    context:place_primitive(primitive)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "elevation-planes");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "a script could not author floor and ceiling Elevation planes");
  auto const& properties = layer.getPrimitive(0)->getProperties();
  require(properties.floorZ == bw::core::Elevation{-4.0f, {0.25f, -0.5f}} &&
              properties.ceilingZ ==
                  bw::core::Elevation{60.0f, {-0.125f, 0.75f}},
          "script-authored Elevation plane values did not reach the Primitive");
}

void meshGeometryEditingUsesTheCurrentPrimitiveTransform() {
  bw::core::ScriptRuntime runtime;
  runtime.load("transform-then-edit", R"(
    local mesh = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    local split_vertex = mesh:split_edge(0)
    mesh:set_position(100, 100)
    assert(mesh:move_vertex(split_vertex, 0, -2))
    context:place_primitive(mesh)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "transform-then-edit");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 1,
          "editing Mesh geometry after changing its transform failed");
  auto* mesh = dynamic_cast<bw::core::MeshPrimitive*>(layer.getPrimitive(0));
  auto proxy = mesh->createEditingProxy();
  wp::Vector2 minimum, maximum;
  proxy->getExtents(minimum, maximum);
  require(minimum.x > 90.0f && minimum.y > 90.0f,
          "a geometry edit used stale pre-transform Mesh coordinates");
}

void scriptCreatedPrimitivesFoldInRecipeOrder() {
  bw::core::ScriptRuntime runtime;
  runtime.load("two", R"(
    local first = context:create_primitive("Rectangle")
    first:set_size(8, 8)
    first:set_position(10, 0)
    context:place_primitive(first)

    local second = context:create_primitive("Rectangle")
    second:set_size(8, 8)
    second:set_position(20, 0)
    context:place_primitive(second)
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
    local only = context:create_primitive("Rectangle")
    only:set_position(10, 0)
    context:place_primitive(only)
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
    local kept = context:create_primitive("Rectangle")
    kept:set_position(10, 0)
    context:place_primitive(kept)

    local dropped = context:create_primitive("Rectangle")
    dropped:set_position(20, 0)
  )");
  runtime.load("raises", R"(
    local dropped = context:create_primitive("Rectangle")
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

  runtime.load("runtime", R"(local p = context:create_primitive("Rectangle")
p:set_position(10, 0)
context:place_primitive(p)
local function explode()
  error("deliberate runtime failure")
end
explode())");

  runtime.load("runaway", R"(local p = context:create_primitive("Rectangle")
p:set_position(20, 0)
context:place_primitive(p)
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

void includeReturnsExecutionLocalModuleTables() {
  bw::core::ScriptRuntime runtime;
  bw::core::ScriptRuntime::IncludedScripts included{{"World/Foo", R"(
        local count = 0
        return {
          next = function()
            count = count + 1
            return count
          end
        }
      )"}};
  runtime.load("root", R"(
    local first = include("World/Foo")
    local second = include("World/Foo")
    record(first == second, first.next(), second.next())
  )",
               included);

  std::vector<int> observations;
  auto bind = [&](sol::environment& environment) {
    environment.set_function(
        "record", [&](bool same, int first, int second) {
          observations.insert(observations.end(), {same ? 1 : 0, first, second});
        });
  };
  runtime.execute("root", bw::core::ScriptLibraries::Build, bind);
  runtime.execute("root", bw::core::ScriptLibraries::Build, bind);
  require(observations == std::vector<int>({1, 1, 2, 1, 1, 2}),
          "include did not cache one returned table per execution or leaked it between executions");

  runtime.load("missing", R"(include("World/Undeclared"))", included);
  bool missingFailed = false;
  try {
    runtime.execute("missing", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const& error) {
    missingFailed = std::string(error.what()).find("not a declared") !=
                    std::string::npos;
  }
  require(missingFailed,
          "include resolved a LuaScript outside the root's declared dependencies");

  runtime.load("not-table", R"(include("World/Value"))",
               {{"World/Value", "return 42"}});
  bool nonTableFailed = false;
  try {
    runtime.execute("not-table", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const& error) {
    nonTableFailed = std::string(error.what()).find("must return exactly one table") !=
                     std::string::npos;
  }
  require(nonTableFailed, "include accepted a chunk that did not return a table");
}

void scriptExecutionsOutputAndErrorsReachTheHostsLogSink() {
  std::vector<bw::core::ScriptLogEvent> events;
  bw::core::ScriptRuntime runtime(
      [&events](bw::core::ScriptLogEvent const& event) {
        events.push_back(event);
      });
  runtime.load("greet", R"(print("hello", 1, true))");

  runtime.execute("greet", bw::core::ScriptLibraries::Build);

  require(events.size() == 2 &&
              events[0].type ==
                  bw::core::ScriptLogEventType::ExecutionStarted &&
              events[0].scriptName == "greet" &&
              events[1].type == bw::core::ScriptLogEventType::Output &&
              events[1].scriptName == "greet" &&
              events[1].message == "hello\t1\ttrue",
          "a script execution and print did not reach the host's log sink");

  runtime.load("broken", R"(error("broken output"))");
  try {
    runtime.execute("broken", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const&) {
  }
  require(events.size() == 4 &&
              events[2].type ==
                  bw::core::ScriptLogEventType::ExecutionStarted &&
              events[2].scriptName == "broken" &&
              events[3].type == bw::core::ScriptLogEventType::Error &&
              events[3].scriptName == "broken" &&
              events[3].message.find("broken output") != std::string::npos,
          "a script failure did not reach the host's log sink as an error");

  events.clear();
  runtime.load("build-log", R"(print("built"))");
  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "build-log");
  step->setName("Scripts");
  events.clear();
  layer.rebuild();
  require(events.size() == 2 && events[0].stepName == "Scripts" &&
              events[1].stepName == "Scripts",
          "a Layer build did not identify its named RunScript step in the log");

  step->setName("");
  events.clear();
  layer.rebuild();
  require(events.size() == 2 && events[0].stepName.empty() &&
              events[1].stepName.empty(),
          "an unnamed RunScript step did not retain its empty log context");
}

void optedInInstructionCountsReachTheLogAtTheEndOfEachRun() {
  std::vector<bw::core::ScriptLogEvent> events;
  bw::core::ScriptRuntime runtime(
      [&events](bw::core::ScriptLogEvent const& event) {
        events.push_back(event);
      },
      {}, true);
  runtime.load("counted", "local value = 1 + 2");

  runtime.execute(
      "counted", bw::core::ScriptLibraries::Build, {}, "Generator");

  std::string const prefix = "Lua instructions executed: ";
  require(events.size() == 2 &&
              events[0].type ==
                  bw::core::ScriptLogEventType::ExecutionStarted &&
              events[1].type == bw::core::ScriptLogEventType::Output &&
              events[1].stepName == "Generator" &&
              events[1].message.starts_with(prefix) &&
              events[1].message.ends_with(" / 1000000") &&
              std::stoull(events[1].message.substr(prefix.size())) > 0,
          "an opted-in host did not receive the instruction count after a run");

  events.clear();
  runtime.load("counted-error", "error('expected failure')");
  try {
    runtime.execute(
        "counted-error", bw::core::ScriptLibraries::Build, {}, "Generator");
  } catch (bw::core::ScriptException const&) {
  }
  require(events.size() == 3 &&
              events[1].type == bw::core::ScriptLogEventType::Error &&
              events[2].type == bw::core::ScriptLogEventType::Output &&
              events[2].message.starts_with(prefix) &&
              events[2].message.ends_with(" / 1000000") &&
              std::stoull(events[2].message.substr(prefix.size())) > 0,
          "a failed run did not finish its log with an instruction count");
}

void debugPrintIsANoOpUnlessTheHostSuppliesASink() {
  std::vector<bw::core::ScriptLogEvent> regularEvents;
  auto regularSink = [&regularEvents](bw::core::ScriptLogEvent const& event) {
    regularEvents.push_back(event);
  };

  bw::core::ScriptRuntime gameRuntime(regularSink);
  gameRuntime.load("debug", R"(dprint("editor only"))");
  gameRuntime.execute("debug", bw::core::ScriptLibraries::Build);
  require(regularEvents.size() == 1 &&
              regularEvents[0].type ==
                  bw::core::ScriptLogEventType::ExecutionStarted,
          "dprint wrote to a runtime which supplied no debug sink");

  regularEvents.clear();
  std::vector<bw::core::ScriptLogEvent> debugEvents;
  bw::core::ScriptRuntime editorRuntime(
      regularSink,
      [&debugEvents](bw::core::ScriptLogEvent const& event) {
        debugEvents.push_back(event);
      });
  editorRuntime.load("debug", R"(dprint("editor only"))");
  editorRuntime.execute(
      "debug", bw::core::ScriptLibraries::Build, {}, "Scripts");
  require(regularEvents.size() == 1 && debugEvents.size() == 1 &&
              debugEvents[0].type == bw::core::ScriptLogEventType::Output &&
              debugEvents[0].scriptName == "debug" &&
              debugEvents[0].stepName == "Scripts" &&
              debugEvents[0].message == "editor only",
          "dprint did not reach the host's debug sink with its log context");

  regularEvents.clear();
  editorRuntime.load("invalid-debug", "dprint(42)");
  try {
    editorRuntime.execute(
        "invalid-debug", bw::core::ScriptLibraries::Build);
  } catch (bw::core::ScriptException const&) {
  }
  require(regularEvents.size() == 2 &&
              regularEvents[1].type == bw::core::ScriptLogEventType::Error &&
              regularEvents[1].message.find("exactly one string") !=
                  std::string::npos &&
              debugEvents.size() == 1,
          "dprint accepted a non-string argument");
}

void theSeedMakesRebuildsReproducibleAndRerollableByChangingIt() {
  bw::core::ScriptRuntime runtime;
  runtime.load("scatter", R"(
    local p = context:create_primitive("Rectangle")
    p:set_position(math.random(0, 1000), 0)
    p:add_audio_emitter(math.random(0, 100), -3, 2, "ambience/wind")
    context:place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "scatter");
  step->setSeed(7);

  layer.rebuild();
  float const firstX = layer.getPrimitive(0)->getPosition().x;
  auto const firstEmitter = layer.getPrimitive(0)->getAudioEmitters().front();
  layer.rebuild();
  float const secondX = layer.getPrimitive(0)->getPosition().x;
  auto const& secondEmitter = layer.getPrimitive(0)->getAudioEmitters().front();
  require(firstX == secondX && firstEmitter.offset == secondEmitter.offset &&
              firstEmitter.heightOffset == secondEmitter.heightOffset &&
              firstEmitter.soundId == secondEmitter.soundId &&
              firstEmitter.guid == secondEmitter.guid && !secondEmitter.guid.empty(),
          "rebuilding the same Layer twice did not reproduce the seed's Primitive and AudioEmitter output");

  step->setSeed(8);
  layer.rebuild();
  float const rerolledX = layer.getPrimitive(0)->getPosition().x;
  auto const rerolledGuid = layer.getPrimitive(0)->getAudioEmitters().front().guid;
  require(rerolledX != firstX && rerolledGuid != firstEmitter.guid,
          "changing the seed did not change the output and derived emitter GUID");

  step->setSeed(7);
  layer.rebuild();
  float const restoredX = layer.getPrimitive(0)->getPosition().x;
  require(restoredX == firstX &&
              layer.getPrimitive(0)->getAudioEmitters().front().guid == firstEmitter.guid,
          "restoring the seed did not restore the previous output and emitter GUID");
}

void scriptsCreateEditListAndRemoveAudioEmittersOnBothPrimitiveTypes() {
  bw::core::ScriptRuntime runtime;
  runtime.load("emitters", R"(
    local primitive = context:create_primitive("Rectangle")
    local emitter = primitive:add_audio_emitter()
    assert(emitter.get_guid == nil and emitter.set_guid == nil)
    emitter:set_offset(4, -5)
    emitter:set_height_offset(6)
    emitter:set_sound_id("events/fire")

    local removed = primitive:add_audio_emitter(1, 2, 3, "remove/me")
    assert(#primitive:get_audio_emitters() == 2)
    assert(primitive:remove_audio_emitter(removed))
    assert(not primitive:remove_audio_emitter(removed))
    local px, py = emitter:get_offset()
    assert(px == 4 and py == -5)
    assert(emitter:get_height_offset() == 6)
    assert(emitter:get_sound_id() == "events/fire")
    context:place_primitive(primitive)

    local mesh = context:create_mesh_primitive({
      {0, 0}, {8, 0}, {8, 8}, {0, 8}
    })
    local mesh_emitter = mesh:add_audio_emitter(-1, 2, 3, "events/water")
    local mx, my = mesh_emitter:get_offset()
    assert(mx == -1 and my == 2)
    assert(#mesh:get_audio_emitters() == 1)
    context:place_primitive(mesh)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* step = addScriptStep(layer, runtime, "emitters");
  step->setSeed(1234);
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 2,
          "using the AudioEmitter API failed the RunScript step");
  auto const& primitiveEmitter = layer.getPrimitive(0)->getAudioEmitters();
  auto const& meshEmitter = layer.getPrimitive(1)->getAudioEmitters();
  require(primitiveEmitter.size() == 1 &&
              primitiveEmitter.front().offset == wp::Vector2{4.0f, -5.0f} &&
              primitiveEmitter.front().heightOffset == 6.0f &&
              primitiveEmitter.front().soundId == "events/fire" &&
              meshEmitter.size() == 1 &&
              meshEmitter.front().offset == wp::Vector2{-1.0f, 2.0f} &&
              meshEmitter.front().heightOffset == 3.0f &&
              meshEmitter.front().soundId == "events/water" &&
              primitiveEmitter.front().guid != meshEmitter.front().guid,
          "AudioEmitter values did not cross both mutable Primitive APIs");
}

void twoRunScriptStepsCannotObserveEachOthersRandomState() {
  bw::core::ScriptRuntime runtime;
  runtime.load("draw", R"(
    local p = context:create_primitive("Rectangle")
    p:set_position(math.random(0, 1000), 0)
    context:place_primitive(p)
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

    local only = context:create_primitive("Rectangle")
    only:set_position(10, 0)
    context:place_primitive(only)
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
    local priors = context:get_build_primitives()
    local p = context:create_primitive("Rectangle")
    p:set_size(4, 4)
    p:set_position(#priors * 10, 0)
    context:place_primitive(p)
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

void scriptsAccessInheritedPrimitiveTransforms() {
  bw::core::ScriptRuntime runtime;
  runtime.load("inherited-transforms", R"(
    local source = context:get_build_primitives()[1]
    local tx, ty = source:get_transform_offset()
    local ex, ey = source:get_influence_eye_origin_offset()
    local epx, epy = source:get_influence_eye_origin_position()
    assert(source:is_static())

    local p = context:create_primitive("Rectangle")
    p:set_size(8, 8)
    p:set_position(epx, epy)
    p:set_transform_offset(tx, ty)
    p:set_orientation(source:get_orientation())
    p:set_follow_orbit_angle(source:get_follow_orbit_angle())
    p:set_influence_eye_origin_offset(ex, ey)
    p:set_influence_eye_angle_offset(source:get_influence_eye_angle_offset())
    context:place_primitive(p)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* source = rectangle(10.0f);
  source->setTransformOffset({3.0f, -4.0f});
  source->setOrientation(27.0f);
  source->setFollowOrbitAngle(true);
  source->setInfluenceEyeOriginOffset({5.0f, 6.0f});
  source->setInfluenceEyeAngleOffset(12.0f);
  layer.getPrimitiveField()->addPrimitive(source);
  auto* step = addScriptStep(layer, runtime, "inherited-transforms");
  layer.rebuild();

  require(!step->hasFailed() && layer.getNumPrimitives() == 2,
          "accessing inherited Primitive transforms failed the script");
  auto const* placed = layer.getPrimitive(1);
  require(placed->getPosition() == wp::Vector2{15.0f, 6.0f} &&
              placed->getTransformOffset() == wp::Vector2{3.0f, -4.0f} &&
              placed->getOrientation() == 27.0f &&
              placed->getFollowOrbitAngle() &&
              placed->getInfluenceEyeOriginOffset() == wp::Vector2{5.0f, 6.0f} &&
              placed->getInfluenceEyeAngleOffset() == 12.0f,
          "inherited Primitive transform values did not cross the Lua API");
}

void aScriptCannotMutateAPriorPrimitive() {
  bw::core::ScriptRuntime runtime;
  runtime.load("mutate", R"(
    local priors = context:get_build_primitives()
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
    local x, y, w, h = context:get_extents()
    local p = context:create_primitive("Rectangle")
    p:set_size(4, 4)
    p:set_position(x + w, y + h)
    context:place_primitive(p)
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
      if #context:find_build_primitives_overlapping(cx - spacing / 2, -spacing / 2, spacing, spacing) == 0 then
        local p = context:create_primitive("Rectangle")
        p:set_size(8, 8)
        p:set_position(cx, 0)
        context:place_primitive(p)
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

void anOverlapQueryIgnoresTheEditorGhost() {
  bw::core::ScriptRuntime runtime;
  runtime.load("ignore-ghost", R"(
    local function overlapping_origin()
      return context:find_build_primitives_overlapping(-4, -4, 8, 8)
    end

    assert(#overlapping_origin() == 0)

    local p = context:create_primitive("Rectangle")
    p:set_size(8, 8)
    context:place_primitive(p)

    assert(#overlapping_origin() == 1)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* ghost = rectangle(0.0f);
  ghost->setFlags(ghost->getFlags() | BW_PRIMITIVE_GHOST_FLAG);
  layer.getPrimitiveField()->addPrimitive(ghost);
  auto* step = addScriptStep(layer, runtime, "ignore-ghost");
  layer.rebuild();

  require(!step->hasFailed(),
          "an overlap query treated the editor ghost as build geometry");
  require(layer.getNumPrimitives() == 2,
          "ignoring the editor ghost also hid the script's own placement");
}

void aScriptPlacesPrefabInstancesOnTheirSizeSpecificGrid() {
  bw::core::ScriptRuntime runtime;
  runtime.load("stamp", R"(
    local prefabs = context:find_define_prefabs("prefabs")
    local rock = prefabs:get_prefab("rock")
    assert(rock:get_tile_size() == 128)
    local tile_x, tile_y = context:get_tile(rock:get_tile_size(), -0.01, -128)
    assert(tile_x == -1 and tile_y == -1)
    context:place_prefab_instance(rock, tile_x, tile_y, 270)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = addPrefabDefinitions(layer, "prefabs", "rock", 0.0f);
  definitions->setPrefabTileSize(
      definitions->getPrefab(0), bw::core::PrefabTileSize::Size128);
  auto* step = addScriptStep(layer, runtime, "stamp");
  layer.rebuild();

  require(!step->hasFailed(), "placing a Prefab instance on its Tile grid failed the step");
  require(layer.getNumPrimitives() == 1, "the placed Prefab instance did not reach the Layer");
  require(layer.getPrimitive(0)->getPosition() == wp::Vector2{-64.0f, -64.0f},
          "the Prefab's tile size did not select its placement grid");
  require(step->ownsPrimitive(layer.getPrimitive(0)),
          "the RunScript step did not own the Primitive it placed as a Prefab instance");
}

void prefabPlacementRejectsNonTilesAndNonQuarterTurns() {
  bw::core::ScriptRuntime runtime;
  runtime.load("fractional-tile", R"(
    local rock = context:find_define_prefabs("prefabs"):get_prefab("rock")
    context:place_prefab_instance(rock, 1.5, 0, 0)
  )");
  runtime.load("invalid-angle", R"(
    local rock = context:find_define_prefabs("prefabs"):get_prefab("rock")
    context:place_prefab_instance(rock, 0, 0, 45)
  )");
  runtime.load("invalid-grid-size", R"(context:get_tile(48, 0, 0))");
  runtime.load("fractional-grid-size", R"(context:get_tile(32.5, 0, 0))");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  addPrefabDefinitions(layer, "prefabs", "rock", 0.0f);
  auto* step = addScriptStep(layer, runtime, "fractional-tile");
  for (auto const* script : {
           "fractional-tile", "invalid-angle", "invalid-grid-size",
           "fractional-grid-size"}) {
    step->setScriptName(script);
    layer.rebuild();
    if (!step->hasFailed() || layer.getNumPrimitives() != 0) {
      throw std::runtime_error(
          std::string("Prefab validation failed for '") + script +
          "': failed=" + (step->hasFailed() ? "true" : "false") +
          ", primitives=" + std::to_string(layer.getNumPrimitives()) +
          ", message='" + step->getFailureMessage() + "'");
    }
  }
}

void placedPrefabInstancesAreCopiesLeavingThePrefabUnchanged() {
  bw::core::ScriptRuntime runtime;
  runtime.load("stamp-twice", R"(
    local prefabs = context:find_define_prefabs("prefabs")
    local rock = prefabs:get_prefab("rock")
    context:place_prefab_instance(rock, 1, 0, 0)
    context:place_prefab_instance(rock, 2, 0, 0)
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = addPrefabDefinitions(layer, "prefabs", "rock", 3.0f);
  auto* step = addScriptStep(layer, runtime, "stamp-twice");
  layer.rebuild();

  require(!step->hasFailed(), "placing two Prefab instances failed the step");
  require(layer.getNumPrimitives() == 2, "both placed instances did not reach the Layer");
  require(at(layer.getPrimitive(0), 99.0f) && at(layer.getPrimitive(1), 163.0f),
          "placed instances were not independently positioned Tile copies");
  require(layer.getPrimitive(0) != layer.getPrimitive(1),
          "two placed instances shared the same Primitive rather than each being a copy");

  auto* sourcePrimitive = definitions->getPrefab(0)->getPrimitive(0);
  require(std::abs(sourcePrimitive->getPosition().x - 3.0f) < .001f,
          "placing transformed instances moved the Prefab's own Primitive");
}

void scriptsListAndFilterPrefabsByTags() {
  bw::core::ScriptRuntime runtime;
  runtime.load("filter-prefabs", R"(
    local definitions = context:find_define_prefabs("prefabs")
    local all = definitions:get_prefabs()
    assert(#all == 3)
    assert(all[1]:get_name() == "first")
    assert(all[2]:get_name() == "second")
    assert(all[3]:get_name() == "third")

    local matches = definitions:get_prefabs_with_tags({"ROCK", "outdoor"})
    assert(#matches == 2)
    assert(matches[1]:get_name() == "first")
    assert(matches[2]:get_name() == "third")
    assert(#definitions:get_prefabs_with_tags({}) == 3)

    local tags = matches[1]:get_tags()
    assert(#tags == 2 and tags[1] == "outdoor" and tags[2] == "rock")
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  definitions->setName("prefabs");
  layer.addStep(definitions);
  auto* first = definitions->addPrefab("first");
  auto* second = definitions->addPrefab("second");
  auto* third = definitions->addPrefab("third");
  definitions->setPrefabTags(first, {"rock", "outdoor"});
  definitions->setPrefabTags(second, {"rock"});
  definitions->setPrefabTags(third, {"rock", "outdoor", "large"});

  auto* step = addScriptStep(layer, runtime, "filter-prefabs");
  layer.rebuild();
  require(!step->hasFailed(),
          "listing or filtering Prefabs by tags failed the script");
}

void scriptsReadAndFilterPrefabTopologyMetadata() {
  bw::core::ScriptRuntime runtime;
  runtime.load("topology-metadata", R"(
    local prefab = context:find_define_prefabs("prefabs"):get_prefab("markers")
    local vertices = prefab:get_metadata_vertices()
    assert(#vertices == 1)
    local x, y = vertices[1]:get_position()
    assert(x == 0 and y == 0)
    local metadata = vertices[1]:get_metadata()
    assert(metadata.kind == "spawn" and metadata.team == "blue")
    assert(#prefab:get_vertices_with_metadata({kind = "spawn"}) == 1)
    assert(#prefab:get_vertices_with_metadata({kind = "spawn", team = "blue"}) == 1)
    assert(#prefab:get_vertices_with_metadata({kind = "exit"}) == 0)
    assert(#prefab:get_vertices_with_metadata({}) == 1)

    local edges = prefab:get_metadata_edges()
    assert(#edges == 1)
    local x1, y1, x2, y2 = edges[1]:get_endpoints()
    assert(x1 == 0 and y1 == 0 and x2 == 10 and y2 == 0)
    metadata = edges[1]:get_metadata()
    assert(metadata.kind == "entrance" and metadata.team == "blue")
    assert(#prefab:get_edges_with_metadata({kind = "entrance"}) == 1)
    assert(#prefab:get_edges_with_metadata({kind = "entrance", team = "blue"}) == 1)
    assert(#prefab:get_edges_with_metadata({kind = "exit"}) == 0)
    assert(#prefab:get_edges_with_metadata({}) == 1)
  )");
  runtime.load("invalid-vertex-metadata-filter", R"(
    local prefab = context:find_define_prefabs("prefabs"):get_prefab("markers")
    prefab:get_vertices_with_metadata({kind = 42})
  )");
  runtime.load("invalid-edge-metadata-filter", R"(
    local prefab = context:find_define_prefabs("prefabs"):get_prefab("markers")
    prefab:get_edges_with_metadata({kind = 42})
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  auto* definitions = new bw::core::DefinePrefabs;
  definitions->setName("prefabs");
  auto const definitionsIndex = layer.addStep(definitions);
  auto* prefab = definitions->addPrefab("markers");
  definitions->setSelectedPrefab(prefab);
  layer.setActiveStep(definitionsIndex);
  bw::core::ClosedPolygon ring{
      {{0.0f, 0.0f}}, {{10.0f, 0.0f}}, {{10.0f, 10.0f}}, {{0.0f, 10.0f}}};
  ring[0].metadata = {{"kind", "spawn"}, {"team", "blue"}};
  ring[0].edgeMetadata = {{"kind", "entrance"}, {"team", "blue"}};
  layer.addPrimitive(bw::core::MeshPrimitive::fromComplexPolygons(
      bw::core::Primitive::Operation::Union, {{ring}}));
  definitions->clearSelectedPrefab();
  layer.setActiveStep(0);

  auto* step = addScriptStep(layer, runtime, "topology-metadata");
  layer.rebuild();
  require(!step->hasFailed(),
          "reading or filtering Prefab topology metadata failed the script");

  for (auto const* invalid : {"invalid-vertex-metadata-filter",
                              "invalid-edge-metadata-filter"}) {
    step->setScriptName(invalid);
    layer.rebuild();
    require(step->hasFailed(),
            "a non-string Prefab topology metadata filter value was accepted");
  }
}

void malformedLuaPrefabTagFiltersFailTheStep() {
  bw::core::ScriptRuntime runtime;
  runtime.load("sparse-tags", R"(
    context:find_define_prefabs("prefabs"):get_prefabs_with_tags({[2] = "rock"})
  )");
  runtime.load("mapped-tags", R"(
    context:find_define_prefabs("prefabs"):get_prefabs_with_tags({kind = "rock"})
  )");
  runtime.load("non-string-tags", R"(
    context:find_define_prefabs("prefabs"):get_prefabs_with_tags({42})
  )");
  runtime.load("invalid-tags", R"(
    context:find_define_prefabs("prefabs"):get_prefabs_with_tags({"not valid"})
  )");

  bw::core::Layer layer(0, "test", 512.0f, 16.0f);
  addPrefabDefinitions(layer, "prefabs", "rock", 0.0f);
  auto* step = addScriptStep(layer, runtime, "sparse-tags");
  for (auto const* script : {
           "sparse-tags", "mapped-tags", "non-string-tags", "invalid-tags"}) {
    step->setScriptName(script);
    layer.rebuild();
    require(step->hasFailed(),
            "a malformed Lua Prefab tag filter did not fail the script");
  }
}

void aScriptReadsAPrimitiveFieldsPrimitivesAsConst() {
  bw::core::ScriptRuntime runtime;
  runtime.load("read-field", R"(
    local field = context:find_primitive_field("authored")
    local primitives = field:get_primitives()
    local p = context:create_primitive("Rectangle")
    p:set_position(#primitives * 10, 0)
    context:place_primitive(p)
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
    local field = context:find_primitive_field("authored")
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
    local p = context:create_primitive("Rectangle")
    p:set_size(8, 8)
    p:set_position(math.random(100, 1000), 0)
    context:place_primitive(p)
  )",
               {}, scriptParameterDefinitions());
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
  sourceStep->setParameterValue("label", std::string("saved"));
  sourceStep->setParameterValue("count", int64_t{9});
  sourceStep->setParameterValue("scale", 3.25);
  sourceStep->setParameterValue("enabled", false);
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
              loadedStep->getParameterValues().size() == 5 &&
              std::get<std::string>(
                  loadedStep->getParameterValues().at("label")) == "saved" &&
              std::get<std::string>(
                  loadedStep->getParameterValues().at("style")) == "rough" &&
              std::get<int64_t>(
                  loadedStep->getParameterValues().at("count")) == 9 &&
              std::get<double>(
                  loadedStep->getParameterValues().at("scale")) == 3.25 &&
              !std::get<bool>(
                  loadedStep->getParameterValues().at("enabled")) &&
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
      [&printed](bw::core::ScriptLogEvent const& event) {
        if (event.type == bw::core::ScriptLogEventType::Output) {
          printed.push_back(event.message);
        }
      });

  auto sourceAt = [](float x, std::string const& message) {
    return "print(\"" + message +
           "\")\n"
           "local p = context:create_primitive(\"Rectangle\")\n"
           "p:set_position(" +
           std::to_string(x) +
           ", 0)\n"
           "context:place_primitive(p)";
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

void coroutinesAdvanceAcrossTicksAndContainFailures() {
  using bw::core::ScriptCoroutineStatus;
  using bw::core::ScriptLibraries;

  bw::core::ScriptRuntime runtime;
  std::vector<std::string> progress;
  runtime.load("progress", R"(
    record("one")
    coroutine.yield()
    record("two")
    coroutine.yield()
    record("three")
  )");

  auto bindProgress = [&](std::string prefix) {
    return [&, prefix = std::move(prefix)](sol::environment& environment) {
      environment.set_function("record", [&, prefix](std::string const& value) {
        progress.push_back(prefix + value);
      });
    };
  };

  auto progressing = runtime.startCoroutine(
      "progress", ScriptLibraries::Base | ScriptLibraries::Coroutine,
      bindProgress("live-"));
  auto abandoned = runtime.startCoroutine(
      "progress", ScriptLibraries::Base | ScriptLibraries::Coroutine,
      bindProgress("abandoned-"));
  require(runtime.getCoroutineStatus(progressing) ==
              ScriptCoroutineStatus::Suspended,
          "a newly started coroutine was not suspended");

  runtime.abandonCoroutine(abandoned);
  require(runtime.getCoroutineStatus(abandoned) ==
              ScriptCoroutineStatus::Abandoned,
          "abandoning a coroutine did not record its terminal status");

  runtime.resumeCoroutine(progressing);
  require(progress == std::vector<std::string>{"live-one"} &&
              runtime.getCoroutineStatus(progressing) ==
                  ScriptCoroutineStatus::Suspended,
          "resuming a coroutine did not advance it to exactly its first yield");
  runtime.tick();
  require(progress ==
              std::vector<std::string>{"live-one", "live-two"},
          "a tick did not advance the live coroutine exactly once");
  runtime.tick();
  require(progress == std::vector<std::string>{
                          "live-one", "live-two", "live-three"} &&
              runtime.getCoroutineStatus(progressing) == ScriptCoroutineStatus::Complete,
          "the coroutine did not progress and complete across several frames");
  runtime.tick();
  require(progress.size() == 3,
          "a terminal or abandoned coroutine remained in the live tick set");

  runtime.load("bad-coroutine", R"(
    coroutine.yield()
    error("coroutine boom")
  )");
  runtime.load("healthy-coroutine", R"(
    record("before")
    coroutine.yield()
    record("after")
  )");
  auto bad = runtime.startCoroutine(
      "bad-coroutine", ScriptLibraries::Base | ScriptLibraries::Coroutine);
  auto healthy = runtime.startCoroutine(
      "healthy-coroutine", ScriptLibraries::Base | ScriptLibraries::Coroutine,
      bindProgress("healthy-"));

  runtime.tick();
  bool reported = false;
  try {
    runtime.tick();
  } catch (bw::core::ScriptException const& error) {
    reported = std::string(error.what()).find("bad-coroutine") !=
                   std::string::npos &&
               std::string(error.what()).find("coroutine boom") !=
                   std::string::npos;
  }
  require(reported && runtime.getCoroutineStatus(bad) ==
                          ScriptCoroutineStatus::Failed,
          "an errored coroutine was not reported and marked failed");
  require(runtime.getCoroutineStatus(healthy) ==
                  ScriptCoroutineStatus::Complete &&
              progress[3] == "healthy-before" &&
              progress[4] == "healthy-after",
          "one coroutine's error stopped or corrupted another live coroutine");
}

void namingAMissingStepOrPrefabFailsTheStepWithTheName() {
  bw::core::ScriptRuntime runtime;
  runtime.load("missing-step", R"(context:find_define_prefabs("nope"))");
  runtime.load("missing-field", R"(context:find_primitive_field("nope"))");
  runtime.load("wrong-type", R"(context:find_define_prefabs("authored"))");
  runtime.load("missing-prefab", R"(
    local prefabs = context:find_define_prefabs("prefabs")
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

    resourceParametersAreExposedThroughTheParamsTable();
    aWorldRoundTripsARunScriptStepAndDeclaresItsResources();
    runScriptDeclaresItsCapabilitiesAndIsGivenItsRuntime();
    theRuntimeCompilesScriptsFromStringsAndCachesThemByName();
    theLibrarySetIsAParameterOfExecution();
    runScriptOperationsAreScopedToTheExecutionContext();
    aScriptCreatesAMeshPrimitiveFromOneRing();
    meshGeometryOperationsKeepIdsWithinOneExecution();
    scriptsMoveAndRemoveMeshSubObjectsById();
    scriptsAuthorAndSliceMeshContainmentByPolygonId();
    meshPrimitivesRetainTheMutablePrimitiveApi();
    scriptsAuthorIndependentElevationPlanes();
    meshGeometryEditingUsesTheCurrentPrimitiveTransform();
    scriptCreatedPrimitivesFoldInRecipeOrder();
    everyExecutionRefillsTheStepsOwnStorage();
    aScriptNeverOwnsWhatItCreates();
    syntaxRuntimeAndBudgetFailuresAreContainedAndHaltTheBuild();
    theBuildEnvironmentDropsFunctionsThatBreakDeterminism();
    includeReturnsExecutionLocalModuleTables();
    scriptExecutionsOutputAndErrorsReachTheHostsLogSink();
    optedInInstructionCountsReachTheLogAtTheEndOfEachRun();
    debugPrintIsANoOpUnlessTheHostSuppliesASink();
    theSeedMakesRebuildsReproducibleAndRerollableByChangingIt();
    scriptsCreateEditListAndRemoveAudioEmittersOnBothPrimitiveTypes();
    twoRunScriptStepsCannotObserveEachOthersRandomState();
    everyExecutionBeginsWithAFreshEnvironment();
    scriptsReadPriorBuildPrimitivesAsConstHandles();
    scriptsAccessInheritedPrimitiveTransforms();
    aScriptCannotMutateAPriorPrimitive();
    aScriptReadsTheLayersExtents();
    aScatterAvoidsExistingGeometryAndItsOwnPlacements();
    anOverlapQueryIgnoresTheEditorGhost();
    aScriptPlacesPrefabInstancesOnTheirSizeSpecificGrid();
    prefabPlacementRejectsNonTilesAndNonQuarterTurns();
    placedPrefabInstancesAreCopiesLeavingThePrefabUnchanged();
    scriptsListAndFilterPrefabsByTags();
    scriptsReadAndFilterPrefabTopologyMetadata();
    malformedLuaPrefabTagFiltersFailTheStep();
    aScriptReadsAPrimitiveFieldsPrimitivesAsConst();
    aScriptCannotMutateAPrimitiveFieldsPrimitiveReadByName();
    reloadingRebuildsExactlyTheLayersThatNameTheScript();
    coroutinesAdvanceAcrossTicksAndContainFailures();
    namingAMissingStepOrPrefabFailsTheStepWithTheName();
    std::cout << "RunScript build step and ScriptRuntime tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
