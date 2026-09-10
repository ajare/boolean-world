#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/BinarySerializer.h>
#include <core/Defines.h>
#include <core/LayerBuildStep.h>
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

  auto yamlLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeYaml(yaml, *yamlLoaded),
          "deep MeshPrimitive YAML did not deserialize");
  checkDeepRoundTrip(*yamlLoaded, Depth);

  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeBinary(serializeBinary(*source), *binaryLoaded),
          "deep MeshPrimitive binary data did not deserialize");
  checkDeepRoundTrip(*binaryLoaded, Depth);
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

  auto conflictingFill = serializeYaml(*source);
  auto fillRule = conflictingFill.find("fillRule: 1");
  require(fillRule != std::string::npos,
          "serialized MeshPrimitive did not report EvenOdd FillRule");
  conflictingFill.replace(fillRule, std::string("fillRule: 1").size(),
                          "fillRule: 0");
  require(!deserializeYaml(conflictingFill, *target),
          "nested MeshPrimitive input with NonZero FillRule was accepted");
  require(containsMessage(target->getDeserializationErrors(),
                          "FillRule must be EvenOdd") &&
              target->getPriority() == 99 &&
              polygonsEqual(target->flattenTree(), beforeTree),
          "conflicting FillRule failure was unclear or changed the target");
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

void authoredCollidesValuesRoundTripThroughSaveAndLoad() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto edgeIndex = proxy->getFirstEdgeIndex();
  auto secondEdgeIndex = proxy->getNextEdgeIndex(edgeIndex);
  require(proxy->setEdgeCollisionOverride(edgeIndex, false) &&
              proxy->setEdgeCollisionOverride(secondEdgeIndex, true),
          "could not author both collision override values before committing");
  proxy->commitTo(*primitive);

  auto const yaml = serializeYaml(*primitive);
  auto loaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeYaml(yaml, *loaded), "authored MeshPrimitive did not deserialize");

  auto loadedProxy = loaded->createEditingProxy();
  auto countOverrides = [](auto const& editingProxy) {
    size_t unsetCount = 0, collidingCount = 0, nonCollidingCount = 0;
    for (auto edge = editingProxy->getFirstEdgeIndex();
         !editingProxy->edgeIndexIterationFinished(edge);
         edge = editingProxy->getNextEdgeIndex(edge)) {
      auto value = editingProxy->getEdgeCollisionOverride(edge);
      if (!value.has_value())
        ++unsetCount;
      else if (*value)
        ++collidingCount;
      else
        ++nonCollidingCount;
    }
    return std::array{unsetCount, collidingCount, nonCollidingCount};
  };
  require(countOverrides(loadedProxy) == std::array<size_t, 3>{2, 1, 1},
          "collision override states did not round-trip through YAML save/load");

  auto const binary = serializeBinary(*primitive);
  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeBinary(binary, *binaryLoaded), "authored MeshPrimitive did not deserialize from binary");
  auto binaryProxy = binaryLoaded->createEditingProxy();
  require(countOverrides(binaryProxy) == std::array<size_t, 3>{2, 1, 1},
          "collision override states did not round-trip through binary save/load");
}

void authoredVisibleValuesRoundTripThroughSaveAndLoad() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto edgeIndex = proxy->getFirstEdgeIndex();
  require(proxy->setEdgeVisible(edgeIndex, false),
          "could not author a non-default visible value before committing");
  proxy->commitTo(*primitive);

  auto const yaml = serializeYaml(*primitive);
  auto loaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeYaml(yaml, *loaded), "authored MeshPrimitive did not deserialize");

  auto loadedProxy = loaded->createEditingProxy();
  size_t visibleCount = 0;
  size_t hiddenCount = 0;
  for (auto edge = loadedProxy->getFirstEdgeIndex();
       !loadedProxy->edgeIndexIterationFinished(edge);
       edge = loadedProxy->getNextEdgeIndex(edge)) {
    if (loadedProxy->getEdgeVisible(edge)) {
      ++visibleCount;
    } else {
      ++hiddenCount;
    }
  }
  require(visibleCount == 3 && hiddenCount == 1,
          "authored visible values did not round-trip through YAML save/load");

  auto const binary = serializeBinary(*primitive);
  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(deserializeBinary(binary, *binaryLoaded), "authored MeshPrimitive did not deserialize from binary");
  auto binaryProxy = binaryLoaded->createEditingProxy();
  visibleCount = 0;
  hiddenCount = 0;
  for (auto edge = binaryProxy->getFirstEdgeIndex();
       !binaryProxy->edgeIndexIterationFinished(edge);
       edge = binaryProxy->getNextEdgeIndex(edge)) {
    if (binaryProxy->getEdgeVisible(edge)) {
      ++visibleCount;
    } else {
      ++hiddenCount;
    }
  }
  require(visibleCount == 3 && hiddenCount == 1,
          "authored visible values did not round-trip through binary save/load");
}

void authoredNormalMapValuesRoundTripAndRejectFutureVersions() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto imageEdge = proxy->getFirstEdgeIndex();
  auto disabledEdge = proxy->getNextEdgeIndex(imageEdge);
  require(proxy->setEdgeNormalMapOverride(
              imageEdge, bw::core::WallNormalMapOverride::image(
                             "normal/directional.png", 12.5f, 0.75f)) &&
              proxy->setEdgeNormalMapOverride(
                  disabledEdge,
                  bw::core::WallNormalMapOverride::disabled()),
          "could not author Wall normal-map states");
  proxy->commitTo(*primitive);

  auto verify = [](MeshPrimitive& loaded) {
    auto editing = loaded.createEditingProxy();
    size_t unset = 0, disabled = 0, image = 0;
    for (auto edge = editing->getFirstEdgeIndex();
         !editing->edgeIndexIterationFinished(edge);
         edge = editing->getNextEdgeIndex(edge)) {
      auto value = editing->getEdgeNormalMapOverride(edge);
      unset += value.state() == bw::core::WallNormalMapOverride::State::Unset;
      disabled += value.state() == bw::core::WallNormalMapOverride::State::Disabled;
      if (auto payload = value.imageData()) {
        ++image;
        require(payload->resourceName == "normal/directional.png" &&
                    payload->repeat == 12.5f &&
                    payload->strength == 0.75f,
                "Image normal-map payload changed on reload");
      } else {
        require(value.state() != bw::core::WallNormalMapOverride::State::Image,
                "inactive normal-map state retained hidden Image payload");
      }
    }
    require(unset == 2 && disabled == 1 && image == 1,
            "Wall normal-map states changed on reload");
  };

  auto yaml = serializeYaml(*primitive);
  auto yamlLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(deserializeYaml(yaml, *yamlLoaded), "normal maps did not load from YAML");
  verify(*yamlLoaded);

  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  auto binaryOk = deserializeBinary(serializeBinary(*primitive), *binaryLoaded);
  std::string binaryErrors;
  for (auto const& error : binaryLoaded->getDeserializationErrors())
    binaryErrors += error + "; ";
  require(binaryOk, "normal maps did not load from binary: " + binaryErrors);
  verify(*binaryLoaded);

  auto marker = yaml.find("edgeOverrideFormat: 7");
  require(marker != std::string::npos, "normal-map format is not versioned");
  yaml.replace(marker, std::string("edgeOverrideFormat: 7").size(),
               "edgeOverrideFormat: 99");
  auto rejected = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(!deserializeYaml(yaml, *rejected) &&
              containsMessage(rejected->getDeserializationErrors(),
                              "Unsupported MeshPrimitive edge override format version"),
          "future Wall normal-map format did not fail clearly");
}

void wallMaskValueValidation() {
  using bw::core::WallMaskOverride;
  using Blend = WallMaskOverride::BlendParameters;

  require(WallMaskOverride::unset().state() == WallMaskOverride::State::Unset &&
              WallMaskOverride::unset().imageData() == nullptr,
          "Unset Wall mask should carry no image payload");
  require(WallMaskOverride::disabled().state() ==
                  WallMaskOverride::State::Disabled &&
              WallMaskOverride::disabled().imageData() == nullptr,
          "Disabled Wall mask should carry no image payload");

  auto const blend = Blend{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
  auto const colour = bw::core::WallMaskOverride::BlendColour{0.25f, 0.5f, 0.75f};
  auto image = WallMaskOverride::image("mask/wear.png", 3, blend, colour);
  require(image.state() == WallMaskOverride::State::Image &&
              image.imageData() != nullptr &&
              image.imageData()->resourceName == "mask/wear.png" &&
              image.imageData()->channel == 3 &&
              image.imageData()->blendParameters == blend &&
              image.imageData()->blendColour == colour,
          "a valid Wall mask Image did not survive construction");

  auto rejected = [](auto&& build) {
    try {
      (void)build();
      return false;
    } catch (std::invalid_argument const&) {
      return true;
    }
  };
  require(rejected([] { return WallMaskOverride::image("", 0, Blend{}); }),
          "an empty Wall mask resource name was accepted");
  require(rejected([] { return WallMaskOverride::image("mask.png", 4, Blend{}); }),
          "an out-of-range Wall mask channel was accepted");
  auto nonFinite = Blend{};
  nonFinite[4] = std::numeric_limits<float>::infinity();
  require(rejected([&] { return WallMaskOverride::image("mask.png", 0, nonFinite); }),
          "a non-finite Wall mask blend parameter was accepted");
  auto nanBlend = Blend{};
  nanBlend[0] = std::numeric_limits<float>::quiet_NaN();
  require(rejected([&] { return WallMaskOverride::image("mask.png", 0, nanBlend); }),
          "a NaN Wall mask blend parameter was accepted");
  auto outOfRangeColour =
      bw::core::WallMaskOverride::BlendColour{0.5f, -0.1f, 0.5f};
  require(rejected([&] {
            return WallMaskOverride::image(
                "mask.png", 0, Blend{}, outOfRangeColour);
          }),
          "an out-of-range Wall mask blend colour was accepted");
}

void authoredWallMaskValuesRoundTripAndRejectFutureVersions() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto imageEdge = proxy->getFirstEdgeIndex();
  auto disabledEdge = proxy->getNextEdgeIndex(imageEdge);
  auto const blend = bw::core::WallMaskOverride::BlendParameters{
      0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
  auto const colour = bw::core::WallMaskOverride::BlendColour{0.25f, 0.5f, 0.75f};
  require(proxy->setEdgeWallMaskOverride(
              imageEdge, bw::core::WallMaskOverride::image(
                             "mask/wear.png", 2, blend, colour)) &&
              proxy->setEdgeWallMaskOverride(
                  disabledEdge, bw::core::WallMaskOverride::disabled()),
          "could not author Wall mask states");
  proxy->commitTo(*primitive);

  auto verify = [&](MeshPrimitive& loaded) {
    auto editing = loaded.createEditingProxy();
    size_t unset = 0, disabled = 0, image = 0;
    for (auto edge = editing->getFirstEdgeIndex();
         !editing->edgeIndexIterationFinished(edge);
         edge = editing->getNextEdgeIndex(edge)) {
      auto value = editing->getEdgeWallMaskOverride(edge);
      unset += value.state() == bw::core::WallMaskOverride::State::Unset;
      disabled += value.state() == bw::core::WallMaskOverride::State::Disabled;
      if (auto payload = value.imageData()) {
        ++image;
        require(payload->resourceName == "mask/wear.png" &&
                    payload->channel == 2 &&
                    payload->blendParameters == blend &&
                    payload->blendColour == colour,
                "Image Wall mask payload changed on reload");
      } else {
        require(value.state() != bw::core::WallMaskOverride::State::Image,
                "inactive Wall mask state retained a hidden Image payload");
      }
    }
    require(unset == 2 && disabled == 1 && image == 1,
            "Wall mask states changed on reload");
  };

  auto yaml = serializeYaml(*primitive);
  auto yamlLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(deserializeYaml(yaml, *yamlLoaded), "Wall masks did not load from YAML");
  verify(*yamlLoaded);

  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  auto binaryOk = deserializeBinary(serializeBinary(*primitive), *binaryLoaded);
  std::string binaryErrors;
  for (auto const& error : binaryLoaded->getDeserializationErrors())
    binaryErrors += error + "; ";
  require(binaryOk, "Wall masks did not load from binary: " + binaryErrors);
  verify(*binaryLoaded);

  auto marker = yaml.find("edgeOverrideFormat: 7");
  require(marker != std::string::npos, "Wall-mask format is not versioned");
  yaml.replace(marker, std::string("edgeOverrideFormat: 7").size(),
               "edgeOverrideFormat: 99");
  auto rejected = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(!deserializeYaml(yaml, *rejected) &&
              containsMessage(rejected->getDeserializationErrors(),
                              "Unsupported MeshPrimitive edge override format version"),
          "future Wall-mask format did not fail clearly");
}

void format4LoadsWallMaskUnsetWithZeroBlendParameters() {
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto imageEdge = proxy->getFirstEdgeIndex();
  auto const blend = bw::core::WallMaskOverride::BlendParameters{
      0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
  require(proxy->setEdgeWallMaskOverride(
              imageEdge, bw::core::WallMaskOverride::image(
                             "mask/wear.png", 1, blend)),
          "could not author a Wall mask before downgrading");
  proxy->commitTo(*primitive);

  auto yaml = serializeYaml(*primitive);
  auto marker = yaml.find("edgeOverrideFormat: 7");
  require(marker != std::string::npos, "Wall-mask format is not versioned");
  yaml.replace(marker, std::string("edgeOverrideFormat: 7").size(),
               "edgeOverrideFormat: 4");

  auto loaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(deserializeYaml(yaml, *loaded), "a format-4 MeshPrimitive did not load");
  auto editing = loaded->createEditingProxy();
  for (auto edge = editing->getFirstEdgeIndex();
       !editing->edgeIndexIterationFinished(edge);
       edge = editing->getNextEdgeIndex(edge)) {
    auto value = editing->getEdgeWallMaskOverride(edge);
    require(value.state() == bw::core::WallMaskOverride::State::Unset &&
                value.imageData() == nullptr,
            "a format-4 file loaded a Wall mask other than Unset");
  }
}

std::string asLegacyCollisionYaml(std::string yaml, bool retainFormat) {
  auto marker = yaml.find("edgeOverrideFormat: 7");
  require(marker != std::string::npos,
          "serialized MeshPrimitive had no edge override format marker");
  auto markerLineStart = yaml.rfind('\n', marker) + 1;
  auto markerLineEnd = yaml.find('\n', marker);
  if (retainFormat) {
    yaml.replace(markerLineStart, markerLineEnd - markerLineStart,
                 "  collisionOverrideFormat: 1");
  } else {
    yaml.erase(markerLineStart, markerLineEnd - markerLineStart + 1);
  }
  for (size_t position = 0;
       (position = yaml.find("normalMapState:", position)) !=
       std::string::npos;) {
    auto lineStart = yaml.rfind('\n', position) + 1;
    auto end = yaml.find('\n', position);
    yaml.erase(lineStart, end - lineStart + 1);
    position = lineStart;
  }
  return yaml;
}

void legacyEdgeOverrideFormatsAreRejected() {
  auto source = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto yaml = asLegacyCollisionYaml(serializeYaml(*source), false);

  size_t position = 0;
  size_t vertexFlagIndex = 0;
  while ((position = yaml.find("        flags: 0", position)) != std::string::npos) {
    if ((vertexFlagIndex++ % 2) != 0) {
      yaml.replace(position, std::string("        flags: 0").size(),
                   "        flags: 1");
    }
    ++position;
  }
  require(vertexFlagIndex == 4,
          "the legacy migration fixture did not rewrite four vertex flags");

  auto loaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(!deserializeYaml(yaml, *loaded) &&
              containsMessage(loaded->getDeserializationErrors(),
                              "Unsupported MeshPrimitive edge override format version"),
          "legacy edge override format did not fail cleanly");
}

void preFeatureEdgeDataIsRejected() {
  auto source = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto yaml = asLegacyCollisionYaml(serializeYaml(*source), false);

  // Simulate a MeshPrimitive saved before this feature existed by stripping
  // every per-vertex "flags" field (the one immediately following a "p:"
  // line) while leaving the unrelated, required Primitive-level "flags"
  // field (the common Primitive flag bitmask) untouched.
  std::string stripped;
  stripped.reserve(yaml.size());
  size_t pos = 0;
  bool previousLineWasVertexPosition = false;
  size_t vertexFlagsStripped = 0;
  while (pos < yaml.size()) {
    auto lineEnd = yaml.find('\n', pos);
    auto lineEndExclusive = lineEnd == std::string::npos ? yaml.size() : lineEnd;
    auto line = yaml.substr(pos, lineEndExclusive - pos);
    auto trimmed = line.substr(line.find_first_not_of(" -"));
    bool isVertexFlagsLine =
        previousLineWasVertexPosition && trimmed.rfind("flags:", 0) == 0;
    if (isVertexFlagsLine) {
      ++vertexFlagsStripped;
    } else {
      stripped += line;
      stripped += '\n';
    }
    previousLineWasVertexPosition = trimmed.rfind("p:", 0) == 0;
    pos = lineEnd == std::string::npos ? yaml.size() : lineEnd + 1;
  }
  require(vertexFlagsStripped == 4,
          "the pre-feature simulation did not remove exactly the four square vertices' flags fields");
  require(stripped.find("flags:") != std::string::npos,
          "the pre-feature simulation also removed the unrelated, required Primitive flags field");

  auto loaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1.0f, -1.0f, 1.0f, 1.0f), {}}}));
  require(!deserializeYaml(stripped, *loaded) &&
              containsMessage(loaded->getDeserializationErrors(),
                              "Unsupported MeshPrimitive edge override format version"),
          "pre-feature edge data did not fail cleanly");
}

void proceduralPrimitiveSchemaRemainsFlat() {
  bw::core::RectanglePolygon rectangle(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 2.0f);
  auto const yaml = serializeYaml(rectangle);
  require(yaml.find("complexPolygons:") != std::string::npos &&
              yaml.find("meshPrimitive:") == std::string::npos,
          "procedural Primitive serialization changed with the Mesh schema");
}

void wallMaskImageIsCollectedAsWorldDependentResource() {
  bw::core::World world(100.0f, 10.0f);
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto proxy = primitive->createEditingProxy();
  auto maskEdge = proxy->getFirstEdgeIndex();
  auto normalEdge = proxy->getNextEdgeIndex(maskEdge);
  require(proxy->setEdgeWallMaskOverride(
              maskEdge, bw::core::WallMaskOverride::image(
                            "mask/wear.png", 0,
                            {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f})) &&
              proxy->setEdgeNormalMapOverride(
                  normalEdge, bw::core::WallNormalMapOverride::image(
                                  "normal/wear.png", 2.0f, 1.0f)),
          "could not author a Wall mask and normal map on the fixture primitive");
  proxy->commitTo(*primitive);

  world.addPrimitive(primitive.release());
  require(world.getDependentResourceNames() ==
              std::vector<std::string>{"mask/wear.png", "normal/wear.png"},
          "the mask image was not collected as a World dependent resource");
}

void triplanarMaterialsAreDirectWorldDependencies() {
  bw::core::World world(100.0f, 10.0f);
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-2.0f, -2.0f, 2.0f, 2.0f), {}}}));
  auto properties = primitive->getProperties();
  properties.floorMaterial =
      bw::core::SurfaceMaterialReference::triplanar("World/ZebraTiles");
  properties.ceilingMaterial =
      bw::core::SurfaceMaterialReference::triplanar("World/AmberTiles");
  properties.wallMaterial =
      bw::core::SurfaceMaterialReference::triplanar("World/ZebraTiles");
  primitive->setProperties(properties);
  world.addPrimitive(primitive.release());

  require(world.getDependentResourceNames() ==
              std::vector<std::string>{"World/AmberTiles", "World/ZebraTiles"},
          "Triplanar materials were not exact sorted World dependencies");
}

void topologyMetadataRoundTripsAndEmptyMetadataIsOmittedFromYaml() {
  auto sourceRing = square(-2.0f, -2.0f, 2.0f, 2.0f);
  sourceRing.front().metadata = {{"kind", "spawn"}, {"team", "blue"}};
  sourceRing.front().edgeMetadata =
      {{"kind", "entrance"}, {"team", "blue"}};
  auto primitive = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{sourceRing, {}}}));

  auto verify = [](MeshPrimitive const& loaded) {
    auto const& vertex = loaded.getShells().front().ring.front();
    require(vertex.metadata ==
                std::map<std::string, std::string>{{"kind", "spawn"},
                                                   {"team", "blue"}},
            "vertex metadata changed on reload");
    require(vertex.edgeMetadata ==
                std::map<std::string, std::string>{{"kind", "entrance"},
                                                   {"team", "blue"}},
            "edge metadata changed on reload");
  };

  auto const yaml = serializeYaml(*primitive);
  require(yaml.find("vertexMetadata") != std::string::npos,
          "non-empty vertex metadata was omitted from YAML");
  require(yaml.find("edgeMetadata") != std::string::npos,
          "non-empty edge metadata was omitted from YAML");
  auto yamlLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(deserializeYaml(yaml, *yamlLoaded),
          "topology metadata did not load from YAML");
  verify(*yamlLoaded);

  auto binaryLoaded = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  require(deserializeBinary(serializeBinary(*primitive), *binaryLoaded),
          "topology metadata did not load from binary");
  verify(*binaryLoaded);

  auto empty = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union, {{square(-1, -1, 1, 1), {}}}));
  auto const emptyYaml = serializeYaml(*empty);
  require(emptyYaml.find("vertexMetadata") == std::string::npos,
          "empty vertex metadata was serialized to YAML");
  require(emptyYaml.find("edgeMetadata") == std::string::npos,
          "empty edge metadata was serialized to YAML");
}

void shippedWorldFixtureUsesTheCurrentSchema() {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(
          std::string(BW_CORE_TEST_RESOURCE_DIR) + "/world-test-1.world.yaml"));
  reader->deserialize();
  bw::core::SerializationWorkData workData{100.0f};
  bw::core::World world;
  auto const loaded = world.deserialize(reader, workData);
  std::string errors;
  for (auto const& error : world.getDeserializationErrors()) {
    errors += error + "; ";
  }
  require(loaded, "the shipped World fixture no longer loads: " + errors);
  require(world.getNumPrimitives() > 0,
          "the shipped fixture did not restore its active Primitives");
  require(world.getDependentResourceNames() ==
              std::vector<std::string>{"JaguarTangentSpaceNormalMap"},
          "the shipped fixture did not retain its ImageResource dependency");
}

}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();

    hierarchyRoundTripsThroughYamlAndBinary();
    failedReadsLeaveTheTargetUnchangedAndRejectLegacyInput();
    aggregateLimitsRejectOversizedInputBeforeCommit();
    authoredCollidesValuesRoundTripThroughSaveAndLoad();
    authoredVisibleValuesRoundTripThroughSaveAndLoad();
    authoredNormalMapValuesRoundTripAndRejectFutureVersions();
    wallMaskValueValidation();
    authoredWallMaskValuesRoundTripAndRejectFutureVersions();
    format4LoadsWallMaskUnsetWithZeroBlendParameters();
    legacyEdgeOverrideFormatsAreRejected();
    preFeatureEdgeDataIsRejected();
    proceduralPrimitiveSchemaRemainsFlat();
    wallMaskImageIsCollectedAsWorldDependentResource();
    triplanarMaterialsAreDirectWorldDependencies();
    topologyMetadataRoundTripsAndEmptyMetadataIsOmittedFromYaml();
    shippedWorldFixtureUsesTheCurrentSchema();
    std::cout << "MeshPrimitive containment tree serialization tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
