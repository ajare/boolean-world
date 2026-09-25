#include "PortalTestSupport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/BinarySerializer.h>
#include <core/CoreException.h>
#include <core/DefaultWorldDataGenerator.h>
#include <core/LayerBuildStep.h>
#include <core/Portal.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

namespace {
using bw::core::AuthoredAperture;
using bw::core::PortalResolutionDiagnostic;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bw::core::RectanglePolygon* addRoom(
    bw::core::World& world, float size = 100.0f) {
  auto* room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  room->setSize(size, size);
  world.addPrimitive(room);
  return room;
}

AuthoredAperture aperture(
    float x, float y, float width = 16.0f, float bottom = 0.0f,
    float top = 24.0f) {
  return {{x, y}, width, bottom, top};
}

std::string serializeWorld(bw::core::World const& world) {
  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  world.serialize(serializer, workData);
  return serializer->getSerializedString();
}

bw::core::World deserializeWorld(std::string const& yaml) {
  auto serializer = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();
  bw::core::World result(200.0f, 10.0f);
  bw::core::SerializationWorkData workData;
  workData.accelGridSize = 10.0f;
  workData.allowEmptyWorld = true;
  require(result.deserialize(serializer, workData),
          "Portal World did not deserialize");
  return result;
}

bw::core::World binaryRoundTrip(bw::core::World const& world) {
  auto writer = std::shared_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::toString());
  bw::core::SerializationWorkData writeWorkData;
  world.serialize(writer, writeWorkData);
  auto reader = std::shared_ptr<bw::core::BinarySerializer>(
      bw::core::BinarySerializer::fromString(writer->getSerializedString()));
  reader->deserialize();
  bw::core::World result(200.0f, 10.0f);
  bw::core::SerializationWorkData readWorkData;
  readWorkData.accelGridSize = 10.0f;
  readWorkData.allowEmptyWorld = true;
  require(result.deserialize(reader, readWorkData),
          "binary Portal World did not deserialize");
  return result;
}

void legacyMigrationIsDeterministicAcrossLoopsAndLayers() {
  bw::core::World source(200, 10);
  auto* room = addRoom(source);
  {
    auto mutation = room->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance)
        .setPoints({{0, 0}, {1, 0}});
  }
  auto yaml = serializeWorld(source);
  yaml += R"(
      nextPortalLoopId: 9
      portalLoops:
        - id: 8
          nextEndpointId: 94
          endpoints:
            - {id: 93, centre: [50, -9], width: 18, bottom: 0, top: 24}
            - {id: 17, centre: [-50, 7], width: 20, bottom: 0, top: 24}
            - {id: 42, centre: [0, 50], width: 16, bottom: 0, top: 24}
          traversalOrder: [{endpointId: 17}, {endpointId: 42}, {endpointId: 93}]
        - id: 2
          nextEndpointId: 94
          endpoints:
            - {id: 93, centre: [50, -30], width: 16, bottom: 0, top: 24}
            - {id: 17, centre: [-50, -30], width: 16, bottom: 0, top: 24}
          traversalOrder: [{endpointId: 93}, {endpointId: 17}]
)";
  // Each Layer restarts its own migration identities and default-name search.
  auto layerStart = yaml.find("\n    - id: 0");
  require(layerStart != std::string::npos, "legacy Layer fixture missing");
  auto secondLayer = yaml.substr(layerStart);
  secondLayer.replace(secondLayer.find("id: 0"), 5, "id: 1");
  auto twoLayers = deserializeWorld(yaml + secondLayer);
  for (auto const* layer : twoLayers.getLayers()) {
    require(layer->getPortals().size() == 5 && layer->getPortal(0)->getName() == "Portal 1" &&
                layer->getPortal(4)->getTargetId() == 3,
            "legacy migration identities or names escaped their owning Layer");
  }
  for (auto const& suffix : {
           "\n      nextPortalId: 4294967294\n      portals: []\n",
           "\n      nextPortalPairId: 0\n      portalPairs: []\n"}) {
    bool rejected = false;
    try { (void)deserializeWorld(yaml + suffix); } catch (std::exception const&) { rejected = true; }
    require(rejected, "exhausted Portal allocator or ambiguous legacy schema accepted");
  }
  auto migrated = deserializeWorld(yaml);
  auto repeated = deserializeWorld(yaml);
  auto canonical = serializeWorld(migrated);
  auto yamlReload = deserializeWorld(canonical);
  auto binaryReload = binaryRoundTrip(migrated);
  require(canonical == serializeWorld(repeated) && canonical == serializeWorld(yamlReload) &&
              canonical == serializeWorld(binaryReload),
          "migration and named-schema round trips must be deterministic");
  require(canonical.find("portalLoops:") == std::string::npos &&
              canonical.find("traversalOrder:") == std::string::npos &&
              canonical.find("nextPortalLoopId:") == std::string::npos,
          "new saves retained authored-loop schema");
  for (auto* candidate : {&migrated, &yamlReload, &binaryReload}) {
    auto const* layer = candidate->getActiveLayer();
    require(layer->getPortals().size() == 5, "migration lost an aperture");
    for (uint32_t id = 0; id < 5; ++id) {
      auto const* portal = layer->getPortal(id);
      require(portal && portal->getName() == std::format("Portal {}", id + 1) &&
                  portal->getTargetId() == std::array<uint32_t, 5>{1, 2, 0, 4, 3}[id],
              "migration ignored old-loop/traversal order or collided local IDs");
    }
    require(layer->getPortal(1)->getAperture().centre == wp::Vector2{0, 50} &&
                layer->getPortal(2)->getAperture().centre == wp::Vector2{50, -9},
            "migration confused storage with traversal order");
    auto data = candidate->getWorldData();
    auto const* cycle = data->findPortalLoop(layer->getId(), 0);
    require(cycle && cycle->active && cycle->traversalOrder == std::vector<uint32_t>{0, 1, 2} &&
                cycle->endpoints.front().aperture.width == 16,
            "migration changed inferred routing or resolved width");
  }
  for (auto const& [before, after] : std::vector<std::pair<std::string, std::string>>{
           {"endpointId: 42", "endpointId: 17"},
           {"endpointId: 42", "endpointId: 999"},
           {"nextEndpointId: 94", "nextEndpointId: 42"}}) {
    auto malformed = yaml;
    malformed.replace(malformed.find(before), before.size(), after);
    bool rejected = false;
    try { (void)deserializeWorld(malformed); } catch (std::exception const&) { rejected = true; }
    require(rejected, "invalid legacy traversal or allocator was accepted");
  }
}

// Build old positional tails explicitly, independent of the current writer's
// schema. A one-Layer v5 World ends in nextPortalId, the empty Portal array,
// then the Layer-array terminator. All earlier fields are unchanged since v1.
void legacyBinaryVersionsAndMixedNamesMigrate() {
  bw::core::World empty(200, 10);
  auto writer = std::shared_ptr<bw::core::BinarySerializer>(bw::core::BinarySerializer::toString());
  bw::core::SerializationWorkData work;
  empty.serialize(writer, work);
  auto base = writer->getSerializedString();
  uint32_t currentVersion{};
  std::memcpy(&currentVersion, base.data() + 4, sizeof(currentVersion));
  require(currentVersion == 5 && base.substr(base.size() - 6) == std::string(6, '\0'),
          "v5 writer did not emit exactly the named-only empty Layer tail");
  base.resize(base.size() - 6);
  auto readBinary = [](std::string const& bytes) {
    auto reader = std::shared_ptr<bw::core::BinarySerializer>(bw::core::BinarySerializer::fromString(bytes));
    reader->deserialize();
    bw::core::World result(200, 10);
    bw::core::SerializationWorkData work;
    work.accelGridSize = 10;
    work.allowEmptyWorld = true;
    require(result.deserialize(reader, work), "legacy binary failed to deserialize");
    return result;
  };
  for (uint32_t version = 1; version <= 4; ++version) {
    auto tail = std::shared_ptr<bw::core::BinarySerializer>(bw::core::BinarySerializer::toString());
    if (version >= 2) {
      tail->writeUint32("nextLegacyId", 1);
      tail->beginArray("legacy");
      tail->beginMap("loop");
      tail->writeUint32("id", 0);
      if (version >= 3) tail->writeUint32("nextEndpointId", 94);
      tail->beginArray("endpoints");
      for (uint32_t slot = 0; slot < 2; ++slot) {
        tail->beginMap("endpoint");
        if (version == 2) tail->writeUint8("id", uint8_t(slot));
        else tail->writeUint32("id", slot == 0 ? 93 : 17);
        tail->writeVector2("centre", slot == 0 ? wp::Vector2{50, -9} : wp::Vector2{-50, 7});
        tail->writeFloat("width", slot == 0 ? 18 : 20);
        tail->writeFloat("bottom", 3);
        tail->writeFloat("top", 27);
        tail->endMap();
      }
      tail->endArray();
      if (version >= 3) {
        tail->beginArray("traversalOrder");
        for (auto id : {17u, 93u}) {
          tail->beginMap("entry");
          tail->writeUint32("endpointId", id);
          tail->endMap();
        }
        tail->endArray();
      }
      tail->endMap();
      tail->endArray();
    }
    if (version == 4) {
      tail->writeUint32("nextPortalId", 3);
      tail->beginArray("portals");
      tail->beginMap("portal");
      tail->writeUint32("id", 2);
      tail->writeString("name", "pOrTaL 1");
      tail->writeVector2("centre", {0, 0});
      tail->writeFloat("width", 16);
      tail->writeFloat("bottom", 0);
      tail->writeFloat("top", 24);
      tail->writeUint32("targetId", 2);
      tail->endMap();
      tail->endArray();
    }
    auto bytes = base + tail->getSerializedString().substr(8) + std::string(1, '\0');
    std::memcpy(bytes.data() + 4, &version, sizeof(version));
    auto loaded = readBinary(bytes);
    auto repeated = readBinary(bytes);
    auto reloaded = binaryRoundTrip(loaded);
    require(serializeWorld(loaded) == serializeWorld(repeated) &&
                serializeWorld(loaded) == serializeWorld(reloaded),
            "binary legacy identities or apertures changed on repeat/reload");
    auto const* layer = loaded.getActiveLayer();
    if (version == 1) {
      require(layer->getPortals().empty(), "v1 World acquired Portals");
    } else {
      auto first = version == 4 ? 3u : 0u;
      auto const* portal = layer->getPortal(first);
      require(portal && portal->getTargetId() == first + 1 &&
                  layer->getPortal(first + 1)->getTargetId() == first &&
                  portal->getName() == (version == 4 ? "Portal 2" : "Portal 1") &&
                  portal->getAperture().centre == (version == 2 ? wp::Vector2{50, -9} : wp::Vector2{-50, 7}) &&
                  portal->getAperture().bottom == 3 && portal->getAperture().top == 27,
              "legacy pair/loop order, aperture, allocator or unique name lost");
    }
    if (version >= 2) {
      auto truncated = bytes.substr(0, bytes.size() - 2);
      bool rejected = false;
      try { (void)readBinary(truncated); } catch (std::exception const&) { rejected = true; }
      require(rejected, "truncated legacy binary was accepted");
    }
  }
}

void namedCyclesFollowStableTargets() {
  bw::core::World world(200, 10);
  auto* room = addRoom(world);
  {
    auto mutation = room->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance)
        .setPoints({{0, 0}, {1, 0}});
  }
  auto* layer = world.getActiveLayer();
  auto a = layer->addPortal(aperture(-50, 7, 28));
  auto b = layer->addPortal(aperture(50, -11, 20, 3, 27));
  auto c = layer->addPortal(aperture(13, 50, 24, 6, 30));
  layer->setPortalTarget(a, c);
  layer->setPortalTarget(c, b);
  layer->setPortalTarget(b, a);
  layer->setPortalName(c, " \tDestination C\r\n");
  for (auto const* invalid : {"  ", "  portal 1  "}) {
    bool rejected = false;
    try { layer->setPortalName(c, invalid); }
    catch (bw::core::CoreException const&) { rejected = true; }
    require(rejected && layer->getPortal(c)->getName() == "Destination C",
            "invalid rename changed authored state");
  }
  auto verify = [&](bw::core::World& copy, std::string const& stage) {
    auto* owner = copy.getActiveLayer();
    require(owner->getPortal(a)->getTargetId() == c &&
            owner->getPortal(c)->getTargetId() == b &&
            owner->getPortal(b)->getTargetId() == a &&
            owner->getPortal(c)->getName() == "Destination C",
            "copy, rename or persistence changed target IDs");
    auto data = copy.getWorldData();
    auto* loop = data->findPortalLoop(owner->getId(), b);
    require(loop != nullptr, "named cycle missing");
    require(loop->active, stage + ": named cycle inactive: " +
        std::string(bw::core::PortalTargetGraphDiagnosticText(
            loop->targetGraphDiagnostic)) + "; " +
        std::string(bw::core::PortalResolutionDiagnosticText(loop->diagnostic)));
    require(loop->traversalOrder == std::vector<uint32_t>{a, c, b},
            "named directed cycle not inferred canonically");
    for (auto const& endpoint : loop->endpoints) {
      auto mapping = bw::core::BuildPortalMapping(*loop, endpoint.endpointId);
      require(endpoint.aperture.width == 20 && !mapping.reversesHandedness() &&
              bw::core::NextPortalEndpoint(*loop, endpoint.endpointId)->endpointId ==
                  owner->getPortal(endpoint.endpointId)->getTargetId(),
              "named cycle bypassed width normalization or ordinary mapping");
      auto const& opening = endpoint.aperture;
      require(data->circleIntersectsWall(opening.centre, 2) < 0 &&
              data->circleIntersectsWall(opening.centre + opening.tangent * 9, 2) >= 0,
              "named cycle did not cut collision aperture while retaining its frame");
    }
    return data;
  };
  auto data = verify(world, "original");
  auto yamlCopy = deserializeWorld(serializeWorld(world));
  verify(yamlCopy, "YAML");
  auto binaryCopy = binaryRoundTrip(world);
  verify(binaryCopy, "binary");
  bw::core::World copy(world);
  verify(copy, "copy");
  bw::core::World assigned(200.0f, 10.0f);
  assigned = world;
  verify(assigned, "assignment");
  bw::core::Layer layerCopy(*layer);
  bw::core::Layer layerAssigned;
  layerAssigned = *layer;
  for (auto const* candidate : {&layerCopy, &layerAssigned}) {
    require(candidate->getPortal(a)->getTargetId() == c &&
            candidate->getPortal(c)->getTargetId() == b &&
            candidate->getPortal(b)->getTargetId() == a &&
            candidate->getPortal(c)->getName() == "Destination C" &&
            candidate->getPortal(c)->getAperture().centre == wp::Vector2{13, 50} &&
            candidate->getNextPortalAllocator() == layer->getNextPortalAllocator(),
            "Layer value operations lost Portal state");
  }
  std::vector<bw::core::PortalSnapshot> snapshots;
  for (auto const& portal : layer->getPortals()) snapshots.push_back({layer->getId(), portal});
  auto resolve = [&] { return bw::core::ResolvePortalLoops(data->getArrangement(), data->getWalls(), snapshots); };
  auto original = resolve();
  std::reverse(snapshots.begin(), snapshots.end());
  auto reordered = resolve();
  require(original.size() == 1 && reordered.size() == 1 &&
          original.front().traversalOrder == reordered.front().traversalOrder &&
          original.front().diagnostic == reordered.front().diagnostic,
          "snapshot storage order changed inferred cycle");
  auto* foreign = world.addLayer("Foreign");
  for (int i = 0; i < 4; ++i) (void)foreign->addPortal(aperture(50, 0));
  bool rejected = false;
  try { layer->setPortalTarget(a, foreign->getPortals().back().getId()); } catch (bw::core::CoreException const&) { rejected = true; }
  require(rejected && layer->getPortal(a)->getTargetId() == c, "invalid target mutated authoring");
  auto crossLayer = serializeWorld(world);
  auto const targetPosition = crossLayer.find("targetId: 2");
  require(targetPosition != std::string::npos, "cross-Layer fixture target missing");
  crossLayer.replace(targetPosition, std::string("targetId: 2").size(), "targetId: 3");
  rejected = false;
  try { (void)deserializeWorld(crossLayer); }
  catch (std::exception const&) { rejected = true; }
  require(rejected, "deserialization resolved a target in a foreign Layer");
  layer->setPortalAperture(c, aperture(13, 50, 24, 6, 31));
  auto invalid = world.getWorldData();
  require(!invalid->findPortalLoop(layer->getId(), a)->active,
          "unequal-height cycle did not fail atomically");
  layer->setPortalTarget(a, b);
  layer->setPortalTarget(c, c);
  auto pair = world.getWorldData();
  auto const* resolvedPair = pair->findPortalLoop(layer->getId(), a);
  require(resolvedPair && resolvedPair->active && resolvedPair->endpoints.size() == 2 &&
          bw::core::NextPortalEndpoint(*resolvedPair, a)->endpointId == b &&
          bw::core::NextPortalEndpoint(*resolvedPair, b)->endpointId == a,
          "two-Portal cycle did not preserve bidirectional routing");
  layer->setPortalTarget(b, c);
  layer->setPortalTarget(c, a);
  layer->removePortal(b);
  require(layer->getPortal(a)->getTargetId() == a && layer->getPortal(c)->getTargetId() == a,
          "destination deletion reconnected a cycle or left dangling targets");
  auto deleted = world.getWorldData();
  auto const* endpoint = deleted->findPortalEndpoint(
      layer->getId(), c);
  require(endpoint && endpoint->targetGraphDiagnostic != bw::core::PortalTargetGraphDiagnostic::None,
          "deleting B from A -> B -> C -> A hid the resulting graph diagnostic");
}

void incompleteNamedTargetGraphsRemainAuthoredAndInactive() {
  bw::core::World world(200.0f, 10.0f);
  auto* room = addRoom(world);
  {
    auto mutation = room->mutate();
    mutation.animation(bw::core::VertexTransformer::Key::OrbitDistance)
        .setPoints({{0, 0}, {1, 0}});
  }
  auto* layer = world.getActiveLayer();
  auto const a = layer->addPortal(aperture(-50.0f, 17.0f));
  auto const b = layer->addPortal(aperture(50.0f, 17.0f));
  auto const unrelated = layer->addPortal(aperture(-50.0f, -25.0f));
  auto const cycleFirst = layer->addPortal(aperture(15.0f, 50.0f));
  auto const cycleSecond = layer->addPortal(aperture(15.0f, -50.0f));
  layer->setPortalTarget(a, b); // A -> B, B -> B: one tail into a cycle.
  layer->setPortalTarget(cycleFirst, cycleSecond);
  layer->setPortalTarget(cycleSecond, cycleFirst);

  auto verifyAuthored = [&](bw::core::World& candidate) {
    auto* owner = candidate.getActiveLayer();
    require(owner->getPortal(a)->getTargetId() == b &&
                owner->getPortal(b)->getTargetId() == b &&
                owner->getPortal(unrelated)->getTargetId() == unrelated &&
                owner->getPortal(cycleFirst)->getTargetId() == cycleSecond &&
                owner->getPortal(cycleSecond)->getTargetId() == cycleFirst,
            "an incomplete target graph was normalized during a value operation");
    auto data = candidate.getWorldData();
    auto const* invalid = data->findPortalLoop(
        owner->getId(), a);
    require(invalid && !invalid->active && invalid->endpoints.size() == 2 &&
                invalid->traversalOrder == std::vector<uint32_t>{a, b},
            "tail-plus-cycle component was omitted, split, or activated");
    auto const* aEndpoint = bw::core::FindPortalEndpoint(*invalid, a);
    auto const* bEndpoint = bw::core::FindPortalEndpoint(*invalid, b);
    require(aEndpoint && bEndpoint && aEndpoint->resolved && bEndpoint->resolved &&
                aEndpoint->diagnostic == PortalResolutionDiagnostic::None &&
                bEndpoint->diagnostic == PortalResolutionDiagnostic::None &&
                aEndpoint->targetGraphDiagnostic ==
                    bw::core::PortalTargetGraphDiagnostic::MissingIncomingReference &&
                bEndpoint->targetGraphDiagnostic ==
                    bw::core::PortalTargetGraphDiagnostic::MultipleIncomingReferences,
            std::format("invalid graph diagnostics were not distinct from aperture resolution: A resolved={} aperture={} graph={}; B resolved={} aperture={} graph={}",
                aEndpoint ? aEndpoint->resolved : false,
                aEndpoint ? static_cast<int>(aEndpoint->diagnostic) : -1,
                aEndpoint ? static_cast<int>(aEndpoint->targetGraphDiagnostic) : -1,
                bEndpoint ? bEndpoint->resolved : false,
                bEndpoint ? static_cast<int>(bEndpoint->diagnostic) : -1,
                bEndpoint ? static_cast<int>(bEndpoint->targetGraphDiagnostic) : -1));
    require(data->circleIntersectsWall(aEndpoint->authored.centre, 2.0f) >= 0 &&
                data->circleIntersectsWall(bEndpoint->authored.centre, 2.0f) >= 0,
            "an invalid target component cut collision apertures");
    auto const* mirror = data->findPortalLoop(
        owner->getId(), unrelated);
    require(mirror && mirror->active &&
                data->circleIntersectsWall(
                    mirror->endpoints.front().aperture.centre, 2.0f) < 0,
            "an invalid component deactivated an unrelated Mirror Portal");
    auto const* otherCycle = data->findPortalLoop(
        owner->getId(), cycleFirst);
    require(otherCycle && otherCycle->active && otherCycle->endpoints.size() == 2 &&
                bw::core::NextPortalEndpoint(*otherCycle, cycleFirst)->endpointId ==
                    cycleSecond,
            "an invalid component deactivated an unrelated multi-Portal cycle");
  };

  verifyAuthored(world);
  auto yamlCopy = deserializeWorld(serializeWorld(world));
  verifyAuthored(yamlCopy);
  auto binaryCopy = binaryRoundTrip(world);
  verifyAuthored(binaryCopy);
  bw::core::World copied(world);
  verifyAuthored(copied);

  auto data = world.getWorldData();
  std::vector<bw::core::PortalSnapshot> snapshots;
  for (auto const& portal : layer->getPortals())
    snapshots.push_back({layer->getId(), portal});
  auto first = bw::core::ResolvePortalLoops(
      data->getArrangement(), data->getWalls(), snapshots);
  std::reverse(snapshots.begin(), snapshots.end());
  auto second = bw::core::ResolvePortalLoops(
      data->getArrangement(), data->getWalls(), snapshots);
  require(first.size() == second.size(),
          "snapshot storage order changed target-component count");
  for (size_t index = 0; index < first.size(); ++index) {
    require(first[index].traversalOrder == second[index].traversalOrder &&
                first[index].targetGraphDiagnostic ==
                    second[index].targetGraphDiagnostic,
            "snapshot storage order changed component order or diagnostics");
    for (auto endpointId : first[index].traversalOrder) {
      require(bw::core::FindPortalEndpoint(first[index], endpointId)
                      ->targetGraphDiagnostic ==
                  bw::core::FindPortalEndpoint(second[index], endpointId)
                      ->targetGraphDiagnostic,
              "snapshot storage order changed a Portal graph diagnostic");
    }
  }

  layer->setPortalTarget(b, a);
  auto completed = world.getWorldData();
  auto const* cycle = completed->findPortalLoop(
      layer->getId(), a);
  require(cycle && cycle->active && cycle->endpoints.size() == 2 &&
              bw::core::NextPortalEndpoint(*cycle, a)->endpointId == b &&
              bw::core::NextPortalEndpoint(*cycle, b)->endpointId == a &&
              layer->getPortal(a)->getAperture().centre ==
                  wp::Vector2{-50.0f, 17.0f},
          "completing the final target did not activate the same Portals and apertures");
}

void namedMirrorsRoundTripAndResolveIndependently() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto first = layer->addPortal(aperture(-50, 17));
  auto second = layer->addPortal(aperture(50, 17));
  require(first == 0 && second == 1,
          "independent Portal and legacy identities are not separate");
  require(layer->getPortal(first)->getName() == "Portal 1" &&
          layer->getPortal(second)->getName() == "Portal 2" &&
          layer->getPortal(first)->getTargetId() == first,
          "Mirror defaults are wrong");
  layer->setPortalName(second, "pOrTaL 2");
  layer->removePortal(first);
  auto replacement = layer->addPortal(aperture(-50, 17));
  require(replacement == 2 && layer->getPortal(replacement)->getName() == "Portal 1",
          "default names should reuse gaps, IDs must not");
  bw::core::Layer namingCopy(*layer);
  auto const next = namingCopy.addPortal(aperture(0, 0));
  require(namingCopy.getPortal(next)->getName() == "Portal 3",
          "automatic naming did not compare existing names case-insensitively");
  auto verify = [&](bw::core::World const& copy) {
    auto const* owner = copy.getActiveLayer();
    require(owner->getNextPortalAllocator() == 3 && owner->getPortals().size() == 2,
            "Mirror allocator or storage lost on copy/load");
    auto const* portal = owner->getPortal(replacement);
    require(portal && portal->getName() == "Portal 1" &&
            portal->getTargetId() == replacement &&
            portal->getAperture().centre == wp::Vector2{-50, 17} &&
            portal->getAperture().width == 16 && portal->getAperture().bottom == 0 &&
            portal->getAperture().top == 24,
            "Mirror authored state or legacy loop lost on copy/load");
  };
  verify(deserializeWorld(serializeWorld(world)));
  verify(binaryRoundTrip(world));
  auto yaml = serializeWorld(world);
  for (auto const& [before, after] : std::vector<std::pair<std::string, std::string>>{
      {"targetId: 1", "targetId: 999"},
      {"targetId: 1", "missingTargetId: 1"},
      {"id: 2", "id: 1"},
      {"nextPortalId: 3", "nextPortalId: 2"},
      {"pOrTaL 2", "portal 1"}}) {
    auto malformed = yaml;
    auto position = malformed.find(before);
    require(position != std::string::npos, "Mirror serialization fixture field missing");
    malformed.replace(position, before.size(), after);
    bool rejected = false;
    try { (void)deserializeWorld(malformed); }
    catch (std::runtime_error const&) { rejected = true; }
    require(rejected, "malformed Mirror target, allocator, or duplicate name was accepted");
  }
  verify(bw::core::World(world));
  bw::core::World assigned(200.0f, 10.0f);
  assigned = world;
  verify(assigned);
  auto data = world.getWorldData();
  auto const* mirror = data->findPortalLoop(layer->getId(), replacement);
  require(mirror && mirror->active && mirror->endpoints.size() == 1 &&
          mirror->traversalOrder == std::vector<uint32_t>{replacement},
          "self target did not generate a singleton cycle");
  auto mapping = bw::core::BuildPortalMapping(*mirror, replacement);
  auto reflected = mapping.transformPoint({-43, 21});
  require(mapping.reversesHandedness() && reflected == wp::Vector2{-57, 21} &&
          mapping.transformElevation(13) == 13,
          "generated singleton did not use true planar reflection");
  require(data->getPortalLiquidAdjacency().empty(), "Mirror contributed Liquid adjacency");
  require(data->getPortalLiquidDiagnostics().empty(), "Mirror contributed a Liquid error");

  for (auto const& [authored, expected] : std::vector<std::pair<AuthoredAperture,
          PortalResolutionDiagnostic>>{
      {aperture(-50, 17, 1), PortalResolutionDiagnostic::InsufficientPlayerWidth},
      {aperture(-50, 17, 16, 0, 1), PortalResolutionDiagnostic::InsufficientPlayerHeight},
      {aperture(0, 0), PortalResolutionDiagnostic::MissingRenderedWall}}) {
    layer->setPortalAperture(replacement, authored);
    data = world.getWorldData();
    mirror = data->findPortalLoop(layer->getId(), replacement);
    require(mirror && !mirror->active && mirror->diagnostic == expected,
            "invalid Mirror did not retain the normal geometric diagnostic");
  }
}

void equalAndUnequalWidthsResolveWithoutChangingAuthoredState() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto const firstPortalId = bw::test::addPortalCycle(layer,
      aperture(-50.0f, 0.0f, 16.0f), aperture(50.0f, 0.0f, 28.0f));

  auto snapshot = world.getWorldData();
  auto const* resolved = snapshot->findPortalLoop(layer->getId(), firstPortalId);
  require(resolved && resolved->active,
          "a fully covered Portal loop did not become active");
  auto const* first = snapshot->findPortalEndpoint(
      layer->getId(), 0);
  auto const* second = snapshot->findPortalEndpoint(
      layer->getId(), 1);
  require(first && second &&
              std::abs(first->aperture.width - 16.0f) < 0.001f &&
              std::abs(second->aperture.width - 16.0f) < 0.001f,
          "stable endpoint lookup did not expose normalized Portal widths");
  require(first->aperture.centre == wp::Vector2{-50.0f, 0.0f} &&
              second->aperture.centre == wp::Vector2{50.0f, 0.0f},
          "normalizing Portal widths moved an endpoint centre");
  require(layer->getPortal(1)
                  ->getAperture()
                  .width == 28.0f,
          "Portal resolution mutated the wider authored aperture");
  require(!first->aperture.wallIndices.empty() &&
              !second->aperture.wallIndices.empty(),
          "a resolved aperture did not retain its snapshot wall coverage");
}

void invalidLoopsStayWholeAndDiagnosable() {
  auto resolve = [](AuthoredAperture first, AuthoredAperture second) {
    bw::core::World world(200.0f, 10.0f);
    addRoom(world);
    auto* layer = world.getActiveLayer();
    auto const firstPortalId = bw::test::addPortalCycle(layer,first, second);
    auto snapshot = world.getWorldData();
    auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), firstPortalId);
    require(portalLoop != nullptr, "selected Portal loop disappeared from generation");
    return *portalLoop;
  };

  auto mismatch = resolve(
      aperture(-50.0f, 0.0f, 16.0f, 0.0f, 24.0f),
      aperture(50.0f, 0.0f, 16.0f, 0.0f, 25.0f));
  require(!mismatch.active &&
              bw::core::FindPortalEndpoint(mismatch, 0)->resolved &&
              bw::core::FindPortalEndpoint(mismatch, 1)->resolved &&
              mismatch.diagnostic ==
                  PortalResolutionDiagnostic::UnequalEndpointHeights,
          "unequal endpoint heights did not retain resolved bounds and a diagnostic");

  auto missing = resolve(
      aperture(-50.0f, 0.0f), aperture(0.0f, 0.0f));
  require(!missing.active &&
              bw::core::FindPortalEndpoint(missing, 1)->diagnostic ==
                  PortalResolutionDiagnostic::MissingRenderedWall &&
              bw::core::FindPortalEndpoint(missing, 0)->diagnostic ==
                  PortalResolutionDiagnostic::OtherEndpointUnresolved,
          "a missing partner wall did not deactivate and diagnose the loop");

  auto incomplete = resolve(
      aperture(-50.0f, 0.0f, 120.0f),
      aperture(50.0f, 0.0f, 120.0f));
  require(!incomplete.active &&
              incomplete.diagnostic ==
                  PortalResolutionDiagnostic::IncompleteRenderedWallCoverage,
          "partial wall coverage did not deactivate the loop");

  auto narrow = resolve(
      aperture(-50.0f, 0.0f, 11.0f),
      aperture(50.0f, 0.0f, 16.0f));
  require(!narrow.active &&
              narrow.diagnostic ==
                  PortalResolutionDiagnostic::InsufficientPlayerWidth,
          "an aperture narrower than the player was not diagnosed");

  auto shortLoop = resolve(
      aperture(-50.0f, 0.0f, 16.0f, 0.0f, 19.0f),
      aperture(50.0f, 0.0f, 16.0f, 8.0f, 27.0f));
  require(!shortLoop.active &&
              shortLoop.diagnostic ==
                  PortalResolutionDiagnostic::InsufficientPlayerHeight,
          "an aperture shorter than the player was not diagnosed");
}

void layerSelectionIncludesCompleteLoopsOnly() {
  bw::core::World world(200.0f, 10.0f);
  auto* first = world.getActiveLayer();
  addRoom(world);
  auto const firstPortalId = bw::test::addPortalCycle(first,
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  auto mirrorId = first->addPortal(aperture(-50, 25));
  auto* second = world.addLayer("Second");

  auto* generator = world.getWorldDataGenerator();
  generator->setLayerSelection(bw::core::SelectLayer(second->getId()));
  auto absent = world.getWorldData();
  require(!absent->findPortalLoop(first->getId(), mirrorId),
          "Mirror participated while its owning Layer was unselected");
  require(absent->findPortalLoop(first->getId(), firstPortalId) == nullptr,
          "a Portal loop participated while its owning Layer was unselected");

  generator->setLayerSelection(bw::core::SelectLayer(first->getId()));
  auto present = world.getWorldData();
  auto const* mirror = present->findPortalLoop(first->getId(), mirrorId);
  require(mirror && mirror->active, "selecting a Layer failed to include its Mirror");
  auto const* resolved = present->findPortalLoop(first->getId(), firstPortalId);
  require(resolved && resolved->active,
          "selecting a Portal's Layer did not include both endpoints");

  // Geometry only on an unselected Layer cannot satisfy either endpoint.
  bw::core::World split(200.0f, 10.0f);
  auto* portalLayer = split.getActiveLayer();
  auto const splitLoop = bw::test::addPortalCycle(portalLayer,
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  auto* geometryLayer = split.addLayer("Geometry");
  split.setActiveLayer(geometryLayer);
  addRoom(split);
  split.getWorldDataGenerator()->setLayerSelection(
      bw::core::SelectLayer(portalLayer->getId()));
  auto noGeometry = split.getWorldData();
  auto const* inactive =
      noGeometry->findPortalLoop(portalLayer->getId(), splitLoop);
  require(inactive && !inactive->active,
          "Portal resolution used geometry absent from the selected generation");
}




void activeAperturesCutWallRenderingCollisionAndExposeFallback() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto firstPortalId = bw::test::addPortalCycle(layer,
      aperture(-50.0f, 0.0f, 16.0f),
      aperture(50.0f, 0.0f, 16.0f));
  auto snapshot = world.getWorldData();
  auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), firstPortalId);
  require(portalLoop && portalLoop->active, "Portal aperture fixture did not resolve");

  auto const* endpoint = snapshot->findPortalEndpoint(
      layer->getId(), 0);
  auto wallIndex = endpoint->aperture.wallIndices.front();
  auto segments = snapshot->getWallCollisionSegments(wallIndex);
  require(segments.size() == 2,
          "active aperture did not split its source wall collision span");
  auto const& opening = endpoint->aperture;
  auto midpoint = opening.centre;
  require(std::ranges::none_of(segments, [&](auto const& segment) {
            return midpoint.distanceToLine(segment.v0, segment.v1) < 0.01f;
          }),
          "source wall collision remained intact inside the aperture");
  require(snapshot->circleIntersectsWall(midpoint, 2.0f) < 0,
          "a collider wholly inside the aperture still hit its source wall");
  auto framePosition = midpoint + opening.tangent * 7.0f;
  require(snapshot->circleIntersectsWall(framePosition, 2.0f) >= 0,
          "the collider did not retain the aperture's solid frame");

  auto replacements = snapshot->getDetail().replacementsFor(
      bw::core::arr::DetailSurfaceKind::Wall, wallIndex);
  auto fallbackCount = std::ranges::count_if(
      replacements, [](auto const& triangle) {
        return triangle.kind ==
               bw::core::arr::DetailTriangleKind::PortalFallback;
      });
  require(snapshot->getDetail().isSuppressed(
              bw::core::arr::DetailSurfaceKind::Wall, wallIndex) &&
              fallbackCount == 2,
          "active aperture did not replace the intact wall with one initialized fallback quad");
  for (auto const& triangle : replacements) {
    if (triangle.kind == bw::core::arr::DetailTriangleKind::PortalFallback) {
      continue;
    }
    wp::Vector2 centre{};
    float elevation = 0.0f;
    for (auto const& vertex : triangle.v) {
      centre += wp::Vector2{vertex.position[0], vertex.position[1]} / 3.0f;
      elevation += vertex.position[2] / 3.0f;
    }
    auto along = std::abs((centre - opening.centre).dot(opening.tangent));
    require(along >= opening.width * 0.5f - 0.01f ||
                elevation <= opening.bottom + 0.01f ||
                elevation >= opening.top - 0.01f,
            "a coplanar wall replacement remained inside the active aperture");
  }
}

void portalCentresSnapOnlyToNearestLegalWallCoverage() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto snapshot = world.getWorldData();
  auto const& arrangement = snapshot->getArrangement();
  auto const& walls = snapshot->getWalls();
  auto requested = aperture(0.0f, 0.0f);

  auto side = bw::core::FindNearestLegalPortalCentre(
      arrangement, walls, requested, requested.width,
      {-48.0f, 10.0f}, 3.0f);
  require(side && side->distanceToSq({-50.0f, 10.0f}) < 0.000001f,
          "Portal centre did not snap and orient to the nearest wall");

  // The centre is clamped far enough from a corner for the complete aperture
  // to remain on rendered wall coverage.
  auto corner = bw::core::FindNearestLegalPortalCentre(
      arrangement, walls, requested, requested.width,
      {-48.0f, 44.0f}, 3.0f);
  require(corner && corner->distanceToSq({-50.0f, 42.0f}) < 0.000001f,
          "Portal wall snap did not keep its full width on the wall");

  auto tooFar = bw::core::FindNearestLegalPortalCentre(
      arrangement, walls, requested, requested.width,
      {-48.0f, 46.0f}, 3.0f);
  require(!tooFar, "Portal centre snapped beyond the three-unit capture distance");

  auto tooTall = requested;
  tooTall.top = 1000.0f;
  auto noVerticalCoverage = bw::core::FindNearestLegalPortalCentre(
      arrangement, walls, tooTall, tooTall.width,
      {-48.0f, 10.0f}, 3.0f);
  require(!noVerticalCoverage,
          "Portal centre snapped to a wall without complete vertical coverage");
}

void canonicalNextEndpointRoutesByStableIdentity() {
  // These IDs deliberately differ from their positions in traversal order.
  std::array<uint32_t, 3> order{17, 42, 5};
  require(bw::core::NextPortalEndpointId(order, 17) == 42 &&
              bw::core::NextPortalEndpointId(order, 42) == 5 &&
              bw::core::NextPortalEndpointId(order, 5) == 17,
          "directed Portal loop routing confused identity with position");
  std::array<uint32_t, 2> twoEndpointOrder{17, 42};
  require(bw::core::NextPortalEndpointId(twoEndpointOrder, 17) == 42 &&
              bw::core::NextPortalEndpointId(twoEndpointOrder, 42) == 17,
          "two-endpoint routing must work in both directions");
  bw::core::ResolvedPortalLoop portalLoop;
  require(portalLoop.endpoints.empty() && portalLoop.traversalOrder.empty(),
          "an unresolved loop fabricated endpoint slots or routing");
  portalLoop.endpoints.resize(2);
  portalLoop.endpoints[0].endpointId = 17;
  portalLoop.endpoints[1].endpointId = 42;
  portalLoop.traversalOrder = {17, 42};
  require(bw::core::FindPortalEndpoint(portalLoop, 42) == &portalLoop.endpoints[1] &&
              bw::core::NextPortalEndpoint(portalLoop, 17) == &portalLoop.endpoints[1] &&
              bw::core::NextPortalEndpoint(portalLoop, 42) == &portalLoop.endpoints[0] &&
              !bw::core::FindPortalEndpoint(portalLoop, 0) &&
              !bw::core::NextPortalEndpoint(portalLoop, 0),
          "resolved Portal routing confused stable identity with array position");
  try {
    [[maybe_unused]] auto next = bw::core::NextPortalEndpointId(order, 99);
    require(false, "unknown endpoint ID was accepted");
  } catch (bw::core::CoreException const&) {
    // A stale ID must not silently select a different endpoint.
  }
  std::array<uint32_t, 1> singleton{17};
  require(bw::core::NextPortalEndpointId(singleton, 17) == 17,
          "generated singleton did not route to itself");

  std::array<uint32_t, 3> duplicate{17, 42, 17};
  try {
    [[maybe_unused]] auto next = bw::core::NextPortalEndpointId(duplicate, 17);
    require(false, "duplicate Portal endpoint identity was accepted");
  } catch (bw::core::CoreException const&) {
  }
}

void canonicalRigidTransformPreservesScaleAndWorldUp() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto firstPortalId = bw::test::addPortalCycle(layer,
      aperture(-50.0f, 0.0f, 16.0f, 2.0f, 26.0f),
      aperture(0.0f, 50.0f, 16.0f, 10.0f, 34.0f));
  auto snapshot = world.getWorldData();
  auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), firstPortalId);
  require(portalLoop && portalLoop->active, "rigid Portal transform fixture did not resolve");
  require(layer->getPortal(0)->getTargetId() == 1 &&
              layer->getPortal(1)->getTargetId() == 0 &&
              bw::core::NextPortalEndpoint(*portalLoop, 0)->endpointId == 1 &&
              bw::core::NextPortalEndpoint(*portalLoop, 1)->endpointId == 0,
          "authored and resolved two-endpoint routes disagree");
  auto reverse = bw::core::BuildPortalMapping(*portalLoop, 1);
  require(reverse.source.centre ==
                  snapshot->findPortalEndpoint(layer->getId(), 1)
                      ->aperture.centre &&
              reverse.destination.centre ==
                  snapshot->findPortalEndpoint(layer->getId(), 0)
                      ->aperture.centre,
          "reverse route did not exit through the first endpoint");
  auto transform = bw::core::BuildPortalMapping(*portalLoop, 0);
  auto vector = transform.transformVector({3.0f, 4.0f});
  require(std::abs(vector.length() - 5.0f) < 0.001f &&
              std::abs(transform.transformElevation(7.0f) - 15.0f) < 0.001f,
          "canonical Portal transform changed horizontal scale or world-up elevation offset");
}

void noPortalWorldKeepsItsKeyedSerializationShapeAndGeometry() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto before = world.getWorldData();
  auto const yaml = serializeWorld(world);
  require(yaml.find("portalLoops") == std::string::npos &&
              yaml.find("nextPortalLoopId") == std::string::npos,
          "an empty Portal collection changed keyed World serialization");
  auto loaded = deserializeWorld(yaml);
  auto after = loaded.getWorldData();
  require(after->getPortalLoops().empty() &&
              before->getTriangles().size() == after->getTriangles().size() &&
              before->getWalls().size() == after->getWalls().size(),
          "a no-Portal World changed generated geometry after reload");
}
void reflectionMappingIsNotAHalfTurn() {
  bw::core::ResolvedAperture frame;
  frame.centre = {13.0f, -7.0f};
  frame.tangent = {0.6f, 0.8f};
  frame.front = {-0.8f, 0.6f};
  frame.bottom = 9.0f;
  frame.top = 33.0f;
  auto reflection = bw::core::BuildPortalReflection(frame);
  bw::core::PortalMapping hop{frame, frame};
  auto near = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
  auto same = [&](wp::Vector2 a, wp::Vector2 b) {
    return near(a.x, b.x) && near(a.y, b.y);
  };
  auto direction = frame.tangent * 3.0f + frame.front * 5.0f;
  auto expected = frame.tangent * 3.0f - frame.front * 5.0f;
  auto point = frame.centre + direction;
  require(same(reflection.transformVector(direction), expected) &&
              same(reflection.transformPoint(point), frame.centre + expected),
          "reflection must preserve tangent and reverse front");
  require(!same(hop.transformVector(direction), expected) &&
              same(hop.transformVector(direction), -direction),
          "reflection must differ from an ordinary half-turn hop");
  require(same(reflection.transformPoint(reflection.transformPoint(point)), point) &&
              same(reflection.transformVector(reflection.transformVector(direction)), direction),
          "two reflections in one aperture must compose to identity");
  require(same(wp::Vector2::fromAngle(
                   reflection.transformYaw(direction.clockwiseAngle()), wp::Clockwise),
               wp::Vector2::fromAngle(expected.clockwiseAngle(), wp::Clockwise)),
          "reflected yaw must agree with reflected direction");
  require(reflection.transformElevation(17.0f) == 17.0f &&
              reflection.elevationOffset() == 0.0 &&
              reflection.reversesHandedness() && !hop.reversesHandedness(),
          "reflection elevation or parity is wrong");
  auto destination = frame;
  destination.centre = {-11.0f, 23.0f};
  destination.tangent = {0.0f, 1.0f};
  destination.front = {-1.0f, 0.0f};
  destination.bottom = -4.0f;
  bw::core::PortalMapping outbound{frame, destination};
  bw::core::PortalMapping inbound{destination, frame};
  require(same(inbound.transformPoint(outbound.transformPoint(point)), point) &&
              near(inbound.transformElevation(outbound.transformElevation(17.0f)), 17.0f),
          "ordinary inverse hops must preserve point and elevation");
  auto destinationReflection = bw::core::BuildPortalReflection(destination);
  require(same(destinationReflection.transformPoint(outbound.transformPoint(point)),
               outbound.transformPoint(reflection.transformPoint(point))),
          "reflection and hop composition must agree in endpoint frames");
  for (auto const& mapping : {reflection, hop, outbound}) {
    auto m = mapping.rendererMatrix();
    auto mapped = mapping.transformPoint(point);
    auto mappedDirection = mapping.transformVector(direction);
    require(near(m[0] * direction.x - m[8] * direction.y, mappedDirection.x) &&
                near(m[2] * direction.x - m[10] * direction.y, -mappedDirection.y) &&
                near(mappedDirection.dot(mappedDirection), direction.dot(direction)),
            "matrix direction or isometry scale is wrong");
    require(near(m[0] * point.x - m[8] * point.y + m[12], mapped.x) &&
                near(m[2] * point.x - m[10] * point.y + m[14], -mapped.y) &&
                near(m[5] * 17.0f + m[13], mapping.transformElevation(17.0f)),
            "matrix and point/elevation mapping disagree");
    require(near(m[0] * m[10] - m[8] * m[2],
                 mapping.reversesHandedness() ? -1.0f : 1.0f) &&
                m[4] == 0.0f && m[5] == 1.0f && m[6] == 0.0f,
            "matrix parity or World-up is wrong");
  }
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    legacyMigrationIsDeterministicAcrossLoopsAndLayers();
    legacyBinaryVersionsAndMixedNamesMigrate();
    namedMirrorsRoundTripAndResolveIndependently();
    namedCyclesFollowStableTargets();
    incompleteNamedTargetGraphsRemainAuthoredAndInactive();
    equalAndUnequalWidthsResolveWithoutChangingAuthoredState();
    invalidLoopsStayWholeAndDiagnosable();
    layerSelectionIncludesCompleteLoopsOnly();
    activeAperturesCutWallRenderingCollisionAndExposeFallback();
    portalCentresSnapOnlyToNearestLegalWallCoverage();
    canonicalNextEndpointRoutesByStableIdentity();
    canonicalRigidTransformPreservesScaleAndWorldUp();
    reflectionMappingIsNotAHalfTurn();
    noPortalWorldKeepsItsKeyedSerializationShapeAndGeometry();
    std::cout << "Portal loop authoring and resolution passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
