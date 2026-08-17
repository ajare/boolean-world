#include <cstdio>
#include <iostream>
#include <memory>
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
      0, {10.0f, 20.0f}, {30.0f, 40.0f}));

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
      0, {10.0f, 20.0f}, {30.0f, 40.0f}));
  source.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {50.0f, 60.0f}, {70.0f, 80.0f}));
  auto const yaml = serializeWorld(source);

  bw::core::World target(100.0f, 10.0f);
  target.setName("existing world");
  target.setDescription("existing configuration");
  target.addPrimitive(makeRectangle());
  target.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {1.0f, 2.0f}, {3.0f, 4.0f}));

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
    require(target.getNumPrimitives() == 8 * (reload + 1),
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
      new bw::core::WorldTriggerLine(0, {10.0f, 20.0f}, {30.0f, 40.0f}));
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
