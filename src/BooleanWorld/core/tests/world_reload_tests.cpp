#include <cstdio>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>

#include <core/CoreException.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::RectanglePolygon* makeRectangle() {
  return new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero,
      1.0f);
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

  bw::core::SerializationWorkData workData{10.0f};
  return world->deserialize(reader, workData);
}

std::string withLastKeyRenamed(std::string yaml, std::string const& key) {
  auto const position = yaml.rfind(key);
  require(position != std::string::npos,
          "serialized world does not contain the key to corrupt");
  yaml.replace(position, key.size(), "invalid" + key);
  return yaml;
}

std::string withoutWedgeGeneration(std::string yaml) {
  auto const marker = yaml.find("  wedgeGeneration:");
  require(marker != std::string::npos,
          "serialized world does not contain Wedge generation settings");
  auto const end = yaml.find("\n  layers:", marker);
  require(end != std::string::npos,
          "serialized Wedge generation settings have no following layers");
  yaml.erase(marker, end - marker + 1);
  return yaml;
}

std::string withWedgeScalar(
    std::string yaml, std::string const& key, std::string const& value) {
  auto const marker = yaml.find(key + ": ");
  require(marker != std::string::npos,
          "serialized world does not contain the Wedge setting to replace");
  auto const valueStart = marker + key.size() + 2;
  auto const end = yaml.find('\n', valueStart);
  yaml.replace(valueStart, end - valueStart, value);
  return yaml;
}

std::string withParentId(std::string yaml, uint32_t index, int32_t parentId) {
  std::string const marker = "parentId: ";
  size_t position = 0;
  for (uint32_t i = 0; i <= index; ++i) {
    position = yaml.find(marker, position);
    require(position != std::string::npos,
            "serialized world does not contain the parent id to replace");
    position += marker.size();
  }

  auto const end = yaml.find('\n', position);
  yaml.replace(position, end - position, std::to_string(parentId));
  return yaml;
}

bool containsMessage(std::vector<std::string> const& messages,
                     std::string const& expected) {
  for (auto const& message : messages) {
    if (message.find(expected) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void reloadRecreatesAccelerationGrids() {
  std::string const path = "world_reload_tests.yaml";

  bw::core::World source(100.0f, 10.0f);
  source.addPrimitive(makeRectangle());
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      {10.0f, 20.0f}, {30.0f, 40.0f}));

  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(path));
  bw::core::SerializationWorkData writeWorkData;
  source.serialize(writer, writeWorkData);
  writer->serialize();

  bw::core::World target;
  for (int reload = 0; reload < 2; ++reload) {
    auto reader = std::shared_ptr<bw::core::Serializer>(
        bw::core::YamlSerializer::fromFile(path));
    reader->deserialize();

    bw::core::SerializationWorkData readWorkData{10.0f};
    require(target.deserialize(reader, readWorkData),
            "world reload failed");
  }

  int gridWidth = 0;
  require(target.getGridSettings(&gridWidth, nullptr, nullptr),
          "world reload did not recreate the primitive acceleration grid");
  require(gridWidth == 10,
          "world reload recreated the primitive acceleration grid with the wrong dimensions");
  require(!target.findPrimitives({{-50.0f, -50.0f}, {50.0f, 50.0f}}).empty(),
          "world reload did not register primitives in the recreated acceleration grid");
  require(!target.findTriggerLines({{0.0f, 0.0f}, {50.0f, 50.0f}}).empty(),
          "world reload did not register trigger lines in the recreated acceleration grid");

  std::remove(path.c_str());
}

void failedDeserializationRetainsTemporaryObjectsAndTargetConfiguration() {
  bw::core::World source(100.0f, 10.0f);
  source.addPrimitive(makeRectangle());
  source.addPrimitive(makeRectangle());
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      {10.0f, 20.0f}, {30.0f, 40.0f}));
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      {50.0f, 60.0f}, {70.0f, 80.0f}));
  auto const yaml = serializeWorld(source);

  bw::core::World target(100.0f, 10.0f);
  target.setName("existing world");
  target.setDescription("existing configuration");
  target.addPrimitive(makeRectangle());
  target.addTriggerLine(new bw::core::WorldTriggerLine(
      {1.0f, 2.0f}, {3.0f, 4.0f}));

  require(!deserializeWorld(withLastKeyRenamed(yaml, "xyRatio"), &target),
          "world with a malformed later primitive deserialized");
  require(target.getName() == "existing world" &&
              target.getDescription() == "existing configuration" &&
              target.getNumPrimitives() == 1 && target.getNumTriggerLines() == 1,
          "failed primitive deserialization changed the target world");

  require(!deserializeWorld(withLastKeyRenamed(yaml, "side"), &target),
          "world with a malformed later trigger line deserialized");
  require(target.getName() == "existing world" &&
              target.getDescription() == "existing configuration" &&
              target.getNumPrimitives() == 1 && target.getNumTriggerLines() == 1,
          "failed trigger-line deserialization changed the target world");
}

void deserializationReusesPrimitiveCreators() {
  bw::core::World source(100.0f, 10.0f);
  for (uint32_t i = 0; i < 8; ++i) {
    auto* primitive = makeRectangle();
    primitive->setId(i);
    source.addPrimitive(primitive);
  }

  auto const yaml = serializeWorld(source);
  bw::core::World target(100.0f, 10.0f);
  for (int reload = 0; reload < 2; ++reload) {
    require(deserializeWorld(yaml, &target),
            "world with repeated primitive constructors did not deserialize");
    // Deserializing replaces a World's Layers wholesale (matching how it
    // already replaces every other World property), so a second reload does
    // not accumulate primitives on top of the first - this loop instead
    // exercises that the primitive-type-creator map is reused correctly
    // across repeated deserialize() calls on the same World.
    require(target.getNumPrimitives() == 8,
            "world did not retain every repeatedly constructed primitive");
    for (uint32_t i = 0; i < target.getNumPrimitives(); ++i) {
      require(dynamic_cast<bw::core::RectanglePolygon*>(target.getPrimitive(i)),
              "primitive constructor did not restore a rectangle");
    }
  }
}

void deserializationPreservesAlwaysUpdateVertices() {
  bw::core::World source(100.0f, 10.0f);
  source.addPrimitive(makeRectangle());

  bw::core::World target(100.0f, 10.0f);
  target.setAlwaysUpdateVertices(true);
  require(deserializeWorld(serializeWorld(source), &target),
          "valid world did not deserialize");
  require(target.getAlwaysUpdateVertices(),
          "deserializing a world reset alwaysUpdateVertices");
}

void wedgeGenerationSettingsValidateAndRoundTripTransactionally() {
  bw::core::World defaults(100.0f, 10.0f);
  auto const expectedDefaults = bw::core::WedgeGenerationParameters{};
  require(defaults.getWedgeGenerationParameters() == expectedDefaults,
          "a new World did not use disabled default Wedge settings");

  auto configured = expectedDefaults;
  configured.enabled = true;
  configured.minimumReach = 5.0f;
  configured.maximumReach = 9.0f;
  configured.minimumDropDownHeight = 2.5f;
  configured.maximumDropDownHeight = 4.5f;
  configured.minimumProjectionDepth = 3.0f;
  configured.maximumProjectionDepth = 6.0f;
  defaults.setWedgeGenerationParameters(configured);
  defaults.addPrimitive(makeRectangle());

  bw::core::World copied(defaults);
  bw::core::World assigned(100.0f, 10.0f);
  assigned = defaults;
  require(copied.getWedgeGenerationParameters() == configured &&
              assigned.getWedgeGenerationParameters() == configured,
          "World copy/assignment lost Wedge generation settings");

  auto const yaml = serializeWorld(defaults);
  bw::core::World loaded(100.0f, 10.0f);
  require(deserializeWorld(yaml, &loaded) &&
              loaded.getWedgeGenerationParameters() == configured,
          "valid Wedge generation settings did not round-trip");

  require(deserializeWorld(withoutWedgeGeneration(yaml), &loaded) &&
              loaded.getWedgeGenerationParameters() == expectedDefaults,
          "a missing Wedge generation block did not restore disabled defaults");
  require(deserializeWorld(yaml, &loaded) &&
              loaded.getWedgeGenerationParameters() == configured &&
              deserializeWorld(withoutWedgeGeneration(yaml), &loaded) &&
              loaded.getWedgeGenerationParameters() == expectedDefaults,
          "repeated reload leaked prior Wedge generation settings");

  loaded.setWedgeGenerationParameters(configured);
  auto invalidYaml = withWedgeScalar(yaml, "minimumReach", "0");
  require(!deserializeWorld(invalidYaml, &loaded) &&
              loaded.getWedgeGenerationParameters() == configured,
          "invalid Wedge settings partially changed the target World");
  invalidYaml = withWedgeScalar(yaml, "minimumReach", "10");
  require(!deserializeWorld(invalidYaml, &loaded) &&
              loaded.getWedgeGenerationParameters() == configured,
          "an inverted Wedge range deserialized or changed the target World");

  auto invalid = configured;
  invalid.maximumProjectionDepth =
      std::numeric_limits<float>::infinity();
  bool rejected = false;
  try {
    loaded.setWedgeGenerationParameters(invalid);
  } catch (std::invalid_argument const&) {
    rejected = true;
  }
  require(rejected && loaded.getWedgeGenerationParameters() == configured,
          "the World setter accepted non-finite Wedge settings");
}

void parentChainsAreValidatedDuringDeserialization() {
  bw::core::World source(100.0f, 10.0f);
  auto* root = makeRectangle();
  auto* child = makeRectangle();
  auto* grandchild = makeRectangle();
  source.addPrimitive(root);
  source.addPrimitive(child);
  source.addPrimitive(grandchild);
  child->setParent(root);
  grandchild->setParent(child);

  auto const yaml = serializeWorld(source);

  bw::core::World cyclicTarget(100.0f, 10.0f);
  require(!deserializeWorld(withParentId(yaml, 0, 2), &cyclicTarget),
          "world with a cyclic primitive parent chain deserialized");
  require(containsMessage(cyclicTarget.getDeserializationErrors(),
                          "parent chain contains a cycle"),
          "cyclic primitive parent chain did not report a clear error");

  bw::core::World unknownParentTarget(100.0f, 10.0f);
  require(deserializeWorld(withParentId(yaml, 0, 999), &unknownParentTarget),
          "world with an unknown primitive parent id did not deserialize");
  require(containsMessage(unknownParentTarget.getDeserializationWarnings(),
                          "Unknown primitive parent id 999"),
          "unknown primitive parent id did not produce a warning");

  bw::core::World rootsTarget(100.0f, 10.0f);
  bw::core::World rootsSource(100.0f, 10.0f);
  rootsSource.addPrimitive(makeRectangle());
  require(deserializeWorld(serializeWorld(rootsSource), &rootsTarget),
          "world with a root primitive did not deserialize");
  require(rootsTarget.getDeserializationWarnings().empty(),
          "the no-parent sentinel produced a deserialization warning");
}

void parentWorldPositionsAreCachedAndInvalidated() {
  auto root = std::unique_ptr<bw::core::RectanglePolygon>(makeRectangle());
  auto child = std::unique_ptr<bw::core::RectanglePolygon>(makeRectangle());
  root->setPosition({10.0f, 20.0f});
  child->setPosition({3.0f, 4.0f});
  child->setParent(root.get());

  child->updateVertexPositions();
  auto const initialVertex = child->getVertices()[0][0][0].p;
  child->updateVertexPositions();
  require(child->getVertices()[0][0][0].p == initialVertex,
          "cached primitive parent position changed transformed vertices");

  root->setPosition({17.0f, 31.0f});
  child->updateVertexPositions();
  require(child->getVertices()[0][0][0].p ==
              initialVertex + wp::Vector2(7.0f, 11.0f),
          "moving a parent did not invalidate the child's cached world position");

  bool rejectedCycle = false;
  try {
    root->setParent(child.get());
  } catch (bw::core::CoreException const&) {
    rejectedCycle = true;
  }
  require(rejectedCycle,
          "setParent accepted a cyclic primitive parent chain");
}

void worldsWithoutGridsFailClearlyInsteadOfDereferencingNull() {
  bw::core::World world;
  auto primitive = std::unique_ptr<bw::core::RectanglePolygon>(makeRectangle());

  bool primitiveRejected = false;
  try {
    world.addPrimitive(primitive.get());
  } catch (bw::core::CoreException const&) {
    primitiveRejected = true;
  }
  require(primitiveRejected,
          "adding a primitive without acceleration grids did not fail clearly");
  require(world.getNumPrimitives() == 0,
          "failed primitive addition without acceleration grids changed the world");

  bool changeRejected = false;
  try {
    world.primitiveChanged(primitive.get());
  } catch (bw::core::CoreException const&) {
    changeRejected = true;
  }
  require(changeRejected,
          "changing a primitive without acceleration grids did not fail clearly");

  auto triggerLine = std::unique_ptr<bw::core::WorldTriggerLine>(
      new bw::core::WorldTriggerLine({10.0f, 20.0f}, {30.0f, 40.0f}));
  bool triggerLineRejected = false;
  try {
    world.addTriggerLine(triggerLine.get());
  } catch (bw::core::CoreException const&) {
    triggerLineRejected = true;
  }
  require(triggerLineRejected,
          "adding a trigger line without acceleration grids did not fail clearly");
  require(world.getNumTriggerLines() == 0,
          "failed trigger-line addition without acceleration grids changed the world");
}

}  // namespace

int main() {
  try {
    reloadRecreatesAccelerationGrids();
    failedDeserializationRetainsTemporaryObjectsAndTargetConfiguration();
    deserializationReusesPrimitiveCreators();
    deserializationPreservesAlwaysUpdateVertices();
    wedgeGenerationSettingsValidateAndRoundTripTransactionally();
    parentChainsAreValidatedDuringDeserialization();
    parentWorldPositionsAreCachedAndInvalidated();
    worldsWithoutGridsFailClearlyInsteadOfDereferencingNull();
    std::cout << "World deserialization and acceleration-grid regressions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
