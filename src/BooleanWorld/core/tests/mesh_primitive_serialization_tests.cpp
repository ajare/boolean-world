#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/BinarySerializer.h>
#include <core/MeshPrimitive.h>
#include <core/RectanglePolygon.h>
#include <core/SerializationWorkData.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {

using bw::core::ClosedPolygon;
using bw::core::MeshFilledRegion;
using bw::core::MeshHole;
using bw::core::MeshPrimitive;
using bw::core::Primitive;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ClosedPolygon square(float left, float bottom, float right, float top) {
  return {{{left, bottom}}, {{right, bottom}}, {{right, top}}, {{left, top}}};
}

double twiceArea(ClosedPolygon const& ring) {
  double area = 0.0;
  for (size_t i = 0; i < ring.size(); ++i) {
    auto const& a = ring[i].p;
    auto const& b = ring[(i + 1) % ring.size()].p;
    area += double(a.x) * b.y - double(b.x) * a.y;
  }
  return area;
}

bool polygonsEqual(
    std::vector<bw::core::ComplexPolygon> const& first,
    std::vector<bw::core::ComplexPolygon> const& second) {
  if (first.size() != second.size()) {
    return false;
  }
  for (size_t polygon = 0; polygon < first.size(); ++polygon) {
    if (first[polygon].size() != second[polygon].size()) {
      return false;
    }
    for (size_t ring = 0; ring < first[polygon].size(); ++ring) {
      if (first[polygon][ring].size() != second[polygon][ring].size()) {
        return false;
      }
      for (size_t vertex = 0; vertex < first[polygon][ring].size(); ++vertex) {
        if (first[polygon][ring][vertex].p !=
            second[polygon][ring][vertex].p) {
          return false;
        }
      }
    }
  }
  return true;
}

std::string serializeYaml(bw::core::Serializable const& value) {
  auto writer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  writer->beginMap("root");
  value.serialize(writer, workData);
  writer->endMap();
  return writer->getSerializedString();
}

bool deserializeYaml(std::string const& yaml, bw::core::Serializable& value) {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  reader->deserialize();
  bw::core::SerializationWorkData workData;
  reader->beginMap("root");
  auto const result = value.deserialize(reader, workData);
  reader->endMap();
  return result;
}

std::string serializeBinary(bw::core::Serializable const& value) {
  auto writer = std::shared_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::toString());
  bw::core::SerializationWorkData workData;
  writer->beginMap("root");
  value.serialize(writer, workData);
  writer->endMap();
  return writer->getSerializedString();
}

bool deserializeBinary(std::string const& data, bw::core::Serializable& value) {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::BinarySerializer::fromString(data));
  reader->deserialize();
  bw::core::SerializationWorkData workData;
  reader->beginMap("root");
  auto const result = value.deserialize(reader, workData);
  reader->endMap();
  return result;
}

std::unique_ptr<MeshPrimitive> makeDeepPrimitive(size_t holeCount) {
  auto boundary = square(-10.0f, -10.0f, 10.0f, 10.0f);
  MeshFilledRegion nested{boundary, {}};
  for (size_t depth = 0; depth < holeCount; ++depth) {
    nested = MeshFilledRegion{boundary, {{boundary, {std::move(nested)}}}};
  }

  auto second = MeshFilledRegion{square(20.0f, -5.0f, 30.0f, 5.0f), {}};
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Difference, {std::move(nested), std::move(second)}));
  primitive->setFlags(37);
  primitive->setMetadata(9182);
  primitive->setPriority(73);
  primitive->setTimeUpdateDistance(125.0f);
  auto properties = primitive->getProperties();
  properties.floorZ = -17.0f;
  properties.ceilingZ = 83.0f;
  primitive->setProperties(properties);
  return primitive;
}

void checkDeepRoundTrip(MeshPrimitive const& loaded, size_t holeCount) {
  require(loaded.getOperation() == Primitive::Operation::Difference &&
              loaded.getFlags() == 37 && loaded.getMetadata() == 9182 &&
              loaded.getPriority() == 73 &&
              loaded.getTimeUpdateDistance() == 125.0f &&
              loaded.getProperties().floorZ == -17.0f &&
              loaded.getProperties().ceilingZ == 83.0f,
          "MeshPrimitive common Primitive state did not round-trip");
  require(loaded.getShells().size() == 2,
          "root Shell sibling order or count did not round-trip");
  require(loaded.getShells()[0].ring.front().p.x <
              loaded.getShells()[1].ring.front().p.x,
          "root Shell sibling order changed");

  auto const* filled = &loaded.getShells().front();
  for (size_t depth = 0; depth < holeCount; ++depth) {
    require(filled->holes.size() == 1 &&
                filled->holes.front().islands.size() == 1,
            "deep alternating containment was truncated");
    auto const& hole = filled->holes.front();
    auto const& island = hole.islands.front();
    require(twiceArea(filled->ring) > 0.0 && twiceArea(hole.ring) > 0.0 &&
                twiceArea(island.ring) > 0.0,
            "deserialization did not restore canonical Ring winding");
    require(filled->ring.data() != hole.ring.data() &&
                hole.ring.data() != island.ring.data(),
            "coincident boundaries did not deserialize as independent values");
    filled = &island;
  }
}

void hierarchyRoundTripsThroughYamlAndBinary() {
  constexpr size_t Depth = 100;
  auto source = makeDeepPrimitive(Depth);
  auto const yaml = serializeYaml(*source);

  auto const meshPosition = yaml.find("meshPrimitive:");
  require(meshPosition != std::string::npos &&
              yaml.find("shells:", meshPosition) != std::string::npos &&
              yaml.find("holes:", meshPosition) != std::string::npos &&
              yaml.find("islands:", meshPosition) != std::string::npos,
          "serialized MeshPrimitive did not contain its nested tree (mesh=" +
              std::to_string(meshPosition) + ", shells=" +
              std::to_string(yaml.find("shells:", meshPosition)) + ", holes=" +
              std::to_string(yaml.find("holes:", meshPosition)) + ", islands=" +
              std::to_string(yaml.find("islands:", meshPosition)) + ")");
  require(yaml.find("complexPolygons:") == std::string::npos,
          "serialized MeshPrimitive retained the flat authored payload");
  require(yaml.find("type:", meshPosition) == std::string::npos &&
              yaml.find("id:", meshPosition) == std::string::npos,
          "serialized tree contains subtype tags or persistent node IDs");

  MeshPrimitive yamlLoaded(
      Primitive::Operation::Union, Primitive::FillRule::NonZero,
      {{square(-1.0f, -1.0f, 1.0f, 1.0f)}});
  require(deserializeYaml(yaml, yamlLoaded),
          "deep MeshPrimitive YAML did not deserialize");
  checkDeepRoundTrip(yamlLoaded, Depth);

  MeshPrimitive binaryLoaded(
      Primitive::Operation::Union, Primitive::FillRule::NonZero,
      {{square(-1.0f, -1.0f, 1.0f, 1.0f)}});
  require(deserializeBinary(serializeBinary(*source), binaryLoaded),
          "deep MeshPrimitive binary data did not deserialize");
  checkDeepRoundTrip(binaryLoaded, Depth);
}

bool containsMessage(
    std::vector<std::string> const& messages, std::string const& text) {
  return std::ranges::any_of(messages, [&](std::string const& message) {
    return message.find(text) != std::string::npos;
  });
}

void failedReadsLeaveTheTargetUnchangedAndRejectLegacyInput() {
  auto source = makeDeepPrimitive(2);
  auto yaml = serializeYaml(*source);
  auto target = makeDeepPrimitive(1);
  target->setPriority(99);
  auto const beforeTree = target->flattenTree();
  auto const beforeMetadata = target->getMetadata();

  auto vertex = yaml.find("- p:", yaml.find("meshPrimitive:"));
  require(vertex != std::string::npos, "test YAML has no tree vertex to corrupt");
  auto lineEnd = yaml.find('\n', vertex);
  yaml.replace(vertex, lineEnd - vertex, "- p: [.nan, 0]");
  require(!deserializeYaml(yaml, *target),
          "MeshPrimitive with a non-finite tree vertex deserialized");
  require(target->getPriority() == 99 && target->getMetadata() == beforeMetadata &&
              polygonsEqual(target->flattenTree(), beforeTree),
          "failed tree validation partially changed the target");

  auto legacy = serializeYaml(*source);
  auto marker = legacy.find("meshPrimitive:");
  require(marker != std::string::npos, "test YAML has no MeshPrimitive map");
  legacy.replace(marker, std::string("meshPrimitive:").size(),
                 "legacyMeshPrimitive:");
  require(!deserializeYaml(legacy, *target),
          "legacy flat MeshPrimitive input was accepted");
  require(containsMessage(target->getDeserializationErrors(), "Legacy flat"),
          "legacy MeshPrimitive input did not produce a clear error");
  require(target->getPriority() == 99 && target->getMetadata() == beforeMetadata &&
              polygonsEqual(target->flattenTree(), beforeTree),
          "legacy input changed the target");
}

void aggregateLimitsRejectOversizedInputBeforeCommit() {
  auto source = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  auto yaml = serializeYaml(*source);
  auto shells = yaml.find("shells:");
  auto firstShell = yaml.find("\n    -", shells);
  require(shells != std::string::npos && firstShell != std::string::npos,
          "could not locate the serialized Shell fixture");
  auto shellBody = yaml.substr(firstShell);
  yaml.erase(firstShell);
  for (size_t i = 0; i < 1025; ++i) {
    yaml += shellBody;
  }

  auto target = makeDeepPrimitive(1);
  auto const before = target->flattenTree();
  require(!deserializeYaml(yaml, *target),
          "MeshPrimitive above the aggregate Ring limit deserialized");
  require(containsMessage(target->getDeserializationErrors(),
                          "aggregate Ring limit") &&
              polygonsEqual(target->flattenTree(), before),
          "aggregate limit failure was unclear or changed the target");
}

void proceduralPrimitiveSchemaRemainsFlat() {
  bw::core::RectanglePolygon rectangle(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 2.0f);
  auto const yaml = serializeYaml(rectangle);
  require(yaml.find("complexPolygons:") != std::string::npos &&
              yaml.find("meshPrimitive:") == std::string::npos,
          "procedural Primitive serialization changed with the Mesh schema");
}

void shippedMeshFixtureUsesTheTreeSchema() {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(
          std::string(BW_CORE_TEST_RESOURCE_DIR) + "/template.yaml"));
  reader->deserialize();
  bw::core::SerializationWorkData workData{100.0f};
  bw::core::World world;
  auto const loaded = world.deserialize(reader, workData);
  std::string errors;
  for (auto const& error : world.getDeserializationErrors()) {
    errors += error + "; ";
  }
  require(loaded,
          "the shipped MeshPrimitive template fixture no longer loads: " + errors);
  require(world.getNumPrimitives() == 2 &&
              dynamic_cast<MeshPrimitive*>(world.getPrimitive(0)) &&
              dynamic_cast<MeshPrimitive*>(world.getPrimitive(1)),
          "the shipped fixture did not restore both MeshPrimitives");
}

}  // namespace

int main() {
  try {
    hierarchyRoundTripsThroughYamlAndBinary();
    failedReadsLeaveTheTargetUnchangedAndRejectLegacyInput();
    aggregateLimitsRejectOversizedInputBeforeCommit();
    proceduralPrimitiveSchemaRemainsFlat();
    shippedMeshFixtureUsesTheTreeSchema();
    std::cout << "MeshPrimitive containment tree serialization tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
