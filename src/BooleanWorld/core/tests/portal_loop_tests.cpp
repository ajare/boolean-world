#include <algorithm>
#include <array>
#include <cmath>
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
  layer->setPortalName(c, "Destination C");
  auto verify = [&](bw::core::World& copy, std::string const& stage) {
    auto* owner = copy.getActiveLayer();
    require(owner->getPortal(a)->getTargetId() == c &&
            owner->getPortal(c)->getTargetId() == b &&
            owner->getPortal(b)->getTargetId() == a &&
            owner->getPortal(c)->getName() == "Destination C",
            "copy, rename or persistence changed target IDs");
    auto data = copy.getWorldData();
    auto* loop = data->findPortalLoop(owner->getId(), bw::core::IndependentPortalLoopId, b);
    require(loop != nullptr, "named cycle missing");
    require(loop->active, stage + ": named cycle inactive: " +
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
  std::vector<bw::core::PortalLoopSnapshot> snapshots;
  for (auto const& portal : layer->getPortals()) snapshots.push_back({layer->getId(), {}, portal});
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
  layer->setPortalAperture(c, aperture(13, 50, 24, 6, 31));
  auto invalid = world.getWorldData();
  require(!invalid->findPortalLoop(layer->getId(), bw::core::IndependentPortalLoopId, a)->active,
          "unequal-height cycle did not fail atomically");
  layer->setPortalTarget(a, b);
  layer->setPortalTarget(c, c);
  auto pair = world.getWorldData();
  auto const* resolvedPair = pair->findPortalLoop(layer->getId(), bw::core::IndependentPortalLoopId, a);
  require(resolvedPair && resolvedPair->active && resolvedPair->endpoints.size() == 2 &&
          bw::core::NextPortalEndpoint(*resolvedPair, a)->endpointId == b &&
          bw::core::NextPortalEndpoint(*resolvedPair, b)->endpointId == a,
          "two-Portal cycle did not preserve bidirectional routing");
  layer->setPortalTarget(c, b);
  layer->removePortal(b);
  require(layer->getPortal(a)->getTargetId() == a && layer->getPortal(c)->getTargetId() == c,
          "destination deletion left dangling targets");
}

void namedMirrorsRoundTripAndResolveIndependently() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto legacy = layer->addPortalLoop(aperture(-50, -25), aperture(50, -25));
  auto first = layer->addPortal(aperture(-50, 17));
  auto second = layer->addPortal(aperture(50, 17));
  require(first == 0 && second == 1 && legacy == 0,
          "independent Portal and legacy identities are not separate");
  require(layer->getPortal(first)->getName() == "Portal 1" &&
          layer->getPortal(second)->getName() == "Portal 2" &&
          layer->getPortal(first)->getTargetId() == first,
          "Mirror defaults are wrong");
  layer->removePortal(first);
  auto replacement = layer->addPortal(aperture(-50, 17));
  require(replacement == 2 && layer->getPortal(replacement)->getName() == "Portal 1",
          "default names should reuse gaps, IDs must not");
  auto verify = [&](bw::core::World const& copy) {
    auto const* owner = copy.getActiveLayer();
    require(owner->getNextPortalAllocator() == 3 && owner->getPortals().size() == 2,
            "Mirror allocator or storage lost on copy/load");
    auto const* portal = owner->getPortal(replacement);
    require(portal && portal->getName() == "Portal 1" &&
            portal->getTargetId() == replacement &&
            portal->getAperture().centre == wp::Vector2{-50, 17} &&
            portal->getAperture().width == 16 && portal->getAperture().bottom == 0 &&
            portal->getAperture().top == 24 && owner->getPortalLoop(legacy),
            "Mirror authored state or legacy loop lost on copy/load");
  };
  verify(deserializeWorld(serializeWorld(world)));
  verify(binaryRoundTrip(world));
  auto yaml = serializeWorld(world);
  for (auto const& [before, after] : std::vector<std::pair<std::string, std::string>>{
      {"targetId: 1", "targetId: 999"},
      {"nextPortalId: 3", "nextPortalId: 2"},
      {"Portal 2", "portal 1"}}) {
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
  auto const* mirror = data->findPortalLoop(layer->getId(),
      bw::core::IndependentPortalLoopId, replacement);
  require(mirror && mirror->active && mirror->endpoints.size() == 1 &&
          mirror->traversalOrder == std::vector<uint32_t>{replacement},
          "self target did not generate a singleton cycle");
  auto mapping = bw::core::BuildPortalMapping(*mirror, replacement);
  auto reflected = mapping.transformPoint({-43, 21});
  require(mapping.reversesHandedness() && reflected == wp::Vector2{-57, 21} &&
          mapping.transformElevation(13) == 13,
          "generated singleton did not use true planar reflection");
  require(data->findPortalLoop(layer->getId(), legacy)->active,
          "Mirror identity collided with a legacy loop");
  for (auto const& adjacency : data->getPortalLiquidAdjacency())
    require(adjacency.loopId != bw::core::IndependentPortalLoopId,
            "Mirror contributed Liquid adjacency");
  for (auto const& diagnostic : data->getPortalLiquidDiagnostics())
    require(diagnostic.loopId != bw::core::IndependentPortalLoopId,
            "Mirror contributed a Liquid error");

  for (auto const& [authored, expected] : std::vector<std::pair<AuthoredAperture,
          PortalResolutionDiagnostic>>{
      {aperture(-50, 17, 1), PortalResolutionDiagnostic::InsufficientPlayerWidth},
      {aperture(-50, 17, 16, 0, 1), PortalResolutionDiagnostic::InsufficientPlayerHeight},
      {aperture(0, 0), PortalResolutionDiagnostic::MissingRenderedWall}}) {
    layer->setPortalAperture(replacement, authored);
    data = world.getWorldData();
    mirror = data->findPortalLoop(layer->getId(), bw::core::IndependentPortalLoopId, replacement);
    require(mirror && !mirror->active && mirror->diagnostic == expected,
            "invalid Mirror did not retain the normal geometric diagnostic");
  }
}

void equalAndUnequalWidthsResolveWithoutChangingAuthoredState() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto const loopId = layer->addPortalLoop(
      aperture(-50.0f, 0.0f, 16.0f), aperture(50.0f, 0.0f, 28.0f));

  auto snapshot = world.getWorldData();
  auto const* resolved = snapshot->findPortalLoop(layer->getId(), loopId);
  require(resolved && resolved->active,
          "a fully covered Portal loop did not become active");
  auto const* first = snapshot->findPortalEndpoint(
      layer->getId(), loopId, 0);
  auto const* second = snapshot->findPortalEndpoint(
      layer->getId(), loopId, 1);
  require(first && second &&
              std::abs(first->aperture.width - 16.0f) < 0.001f &&
              std::abs(second->aperture.width - 16.0f) < 0.001f,
          "stable endpoint lookup did not expose normalized Portal widths");
  require(first->aperture.centre == wp::Vector2{-50.0f, 0.0f} &&
              second->aperture.centre == wp::Vector2{50.0f, 0.0f},
          "normalizing Portal widths moved an endpoint centre");
  require(layer->getPortalLoop(loopId)
                  ->findEndpoint(1)
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
    auto const loopId = layer->addPortalLoop(first, second);
    auto snapshot = world.getWorldData();
    auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), loopId);
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
  auto const loopId = first->addPortalLoop(
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  auto mirrorId = first->addPortal(aperture(-50, 25));
  auto* second = world.addLayer("Second");

  auto* generator = world.getWorldDataGenerator();
  generator->setLayerSelection(bw::core::SelectLayer(second->getId()));
  auto absent = world.getWorldData();
  require(!absent->findPortalLoop(first->getId(), bw::core::IndependentPortalLoopId, mirrorId),
          "Mirror participated while its owning Layer was unselected");
  require(absent->findPortalLoop(first->getId(), loopId) == nullptr,
          "a Portal loop participated while its owning Layer was unselected");

  generator->setLayerSelection(bw::core::SelectLayer(first->getId()));
  auto present = world.getWorldData();
  auto const* mirror = present->findPortalLoop(first->getId(), bw::core::IndependentPortalLoopId, mirrorId);
  require(mirror && mirror->active, "selecting a Layer failed to include its Mirror");
  auto const* resolved = present->findPortalLoop(first->getId(), loopId);
  require(resolved && resolved->active,
          "selecting a Portal's Layer did not include both endpoints");

  // Geometry only on an unselected Layer cannot satisfy either endpoint.
  bw::core::World split(200.0f, 10.0f);
  auto* portalLayer = split.getActiveLayer();
  auto const splitLoop = portalLayer->addPortalLoop(
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

void identityAndEndpointStateRoundTripCopyAndAssignment() {
  bw::core::World source(200.0f, 10.0f);
  addRoom(source);
  auto* layer = source.getActiveLayer();
  auto removed = layer->addPortalLoop(
      aperture(-50.0f, -20.0f), aperture(50.0f, -20.0f));
  layer->removePortalLoop(removed);
  auto const loopId = layer->addPortalLoop(
      aperture(-50.0f, 7.0f, 18.0f, 3.0f, 27.0f),
      aperture(50.0f, -9.0f, 22.0f, 5.0f, 29.0f));

  bw::core::World copied(source);
  bw::core::World assigned(100.0f, 10.0f);
  assigned = source;
  auto loaded = deserializeWorld(serializeWorld(source));
  auto binaryLoaded = binaryRoundTrip(source);
  for (auto const* candidate : {&copied, &assigned, &loaded, &binaryLoaded}) {
    auto const* portalLoop = candidate->getActiveLayer()->getPortalLoop(loopId);
    require(portalLoop && portalLoop->getId() == loopId,
            "Portal loop identity did not survive a value operation");
    auto const& first = portalLoop->findEndpoint(0)->getAperture();
    auto const& second = portalLoop->findEndpoint(1)->getAperture();
    require(first.centre == wp::Vector2{-50.0f, 7.0f} &&
                first.width == 18.0f && first.bottom == 3.0f &&
                first.top == 27.0f &&
                second.centre == wp::Vector2{50.0f, -9.0f} &&
                second.width == 22.0f && second.bottom == 5.0f &&
                second.top == 29.0f,
            "Portal endpoint state did not survive a value operation");
  }

  auto const yaml = serializeWorld(source);
  require(yaml.find("wallIndices") == std::string::npos &&
              yaml.find("edge:") == std::string::npos,
          "generated wall identity leaked into Portal serialization");

  bw::core::World deletedLoopWorld(200.0f, 10.0f);
  auto* deletedLoopLayer = deletedLoopWorld.getActiveLayer();
  auto const deletedId = deletedLoopLayer->addPortalLoop(
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  deletedLoopLayer->removePortalLoop(deletedId);
  auto yamlAfterDelete = deserializeWorld(serializeWorld(deletedLoopWorld));
  auto binaryAfterDelete = binaryRoundTrip(deletedLoopWorld);
  require(yamlAfterDelete.getActiveLayer()->addPortalLoop(
              aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f)) == 1 &&
              binaryAfterDelete.getActiveLayer()->addPortalLoop(
                  aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f)) == 1,
          "reload reused a deleted Portal loop's stable id");

  auto generationInput = bw::core::snapshotPortalLoops(
      source, bw::core::SelectAllLayers());
  auto changed = layer->getPortalLoop(loopId)
                     ->findEndpoint(0)
                     ->getAperture();
  changed.centre = {-25.0f, 25.0f};
  layer->setPortalEndpointAperture(loopId, 0, changed);
  require(generationInput.size() == 1 &&
              generationInput.front().loop.getTraversalOrder()[0] == 0 &&
              generationInput.front().loop.findEndpoint(0)
                      ->getAperture()
                      .centre == wp::Vector2{-50.0f, 7.0f},
          "asynchronous generation input did not retain endpoint identity and state");
}

void serializedEndpointSequenceCarriesExplicitTraversalOrder() {
  bw::core::World source(200.0f, 10.0f);
  addRoom(source);
  auto* layer = source.getActiveLayer();
  auto const loopId = layer->addPortalLoop(
      aperture(-50.0f, 7.0f), aperture(50.0f, -9.0f));

  require(layer->movePortalEndpointEarlier(loopId, 1),
          "two-endpoint Portal loop could not be reordered");
  auto yaml = serializeWorld(source);
  auto const endpoints = yaml.find("endpoints:", yaml.find("portalLoops:"));
  auto const order = yaml.find("traversalOrder:", endpoints);
  require(endpoints != std::string::npos && order != std::string::npos &&
              yaml.find("endpointId: 1", order) <
                  yaml.find("endpointId: 0", order),
          "Portal loop serialization omitted explicit traversal order");

  auto loaded = deserializeWorld(yaml);
  auto const* loadedLoop = loaded.getActiveLayer()->getPortalLoop(loopId);
  require(loadedLoop && loadedLoop->getTraversalOrder()[0] == 1 &&
              loadedLoop->getTraversalOrder()[1] == 0 &&
              loadedLoop->findEndpoint(0)->getAperture().centre ==
                  wp::Vector2{-50.0f, 7.0f} &&
              loadedLoop->findEndpoint(1)->getAperture().centre ==
                  wp::Vector2{50.0f, -9.0f},
          "deserialization confused explicit traversal order with endpoint identity");

  auto snapshot = loaded.getWorldData();
  auto const* resolved = snapshot->findPortalLoop(
      loaded.getActiveLayer()->getId(), loopId);
  require(resolved && resolved->traversalOrder ==
                          std::vector<uint32_t>{1, 0} &&
              snapshot->findPortalEndpoint(
                  loaded.getActiveLayer()->getId(), loopId, 1)
                      ->authored.centre == wp::Vector2{50.0f, -9.0f},
          "generation did not preserve serialized endpoint identity and order");

  auto const saved = serializeWorld(loaded);
  auto const savedOrder =
      saved.find("traversalOrder:", saved.find("portalLoops:"));
  require(saved.find("endpointId: 1", savedOrder) <
              saved.find("endpointId: 0", savedOrder),
          "saving changed the explicit loop traversal order or its format");
}

void orderedLoopsPreserveStableIdentityAndResolveEveryEndpoint() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto const loopId = layer->addPortalLoop(
      aperture(-50.0f, 0.0f, 20.0f),
      aperture(50.0f, 0.0f, 18.0f));
  auto const thirdId = layer->addPortalEndpointAfter(
      loopId, 0, aperture(0.0f, 50.0f, 14.0f));
  auto const* loop = layer->getPortalLoop(loopId);
  require(thirdId == 2 &&
              std::ranges::equal(
                  loop->getTraversalOrder(),
                  std::array<uint32_t, 3>{0, 2, 1}) &&
              loop->getNextEndpointId(0) == 2 &&
              loop->getNextEndpointId(2) == 1 &&
              loop->getNextEndpointId(1) == 0,
          "adding a Portal endpoint did not splice the directed loop");

  require(layer->movePortalEndpointLater(loopId, thirdId) &&
              loop->getTraversalOrder()[1] == 1 &&
              loop->getTraversalOrder()[2] == thirdId &&
              layer->movePortalEndpointEarlier(loopId, thirdId) &&
              loop->getTraversalOrder()[1] == thirdId,
          "reordering a Portal endpoint did not preserve its stable identity");
  layer->removePortalEndpoint(loopId, thirdId);
  auto const replacementId = layer->addPortalEndpointAfter(
      loopId, 0, aperture(0.0f, 50.0f, 14.0f));
  require(replacementId == 3 &&
              !loop->findEndpoint(thirdId) &&
              loop->getNextEndpointId(0) == replacementId,
          "Portal endpoint deletion reused an identity or failed to reconnect neighbours");

  auto snapshot = world.getWorldData();
  auto const* resolved = snapshot->findPortalLoop(layer->getId(), loopId);
  require(resolved && resolved->active && resolved->endpoints.size() == 3 &&
              std::ranges::all_of(
                  resolved->endpoints,
                  [](auto const& endpoint) {
                    return endpoint.resolved &&
                           std::abs(endpoint.aperture.width - 14.0f) < 0.001f;
                  }) &&
              bw::core::NextPortalEndpoint(*resolved, 0)->endpointId ==
                  replacementId &&
              bw::core::NextPortalEndpoint(*resolved, replacementId)
                      ->endpointId == 1 &&
              bw::core::NextPortalEndpoint(*resolved, 1)->endpointId == 0,
          "three-endpoint Portal loop did not resolve and route as one directed cycle");

  auto const yaml = serializeWorld(world);
  require(yaml.find("portalLoops:") != std::string::npos &&
              yaml.find("traversalOrder:") != std::string::npos &&
              yaml.find("portalPairs:") == std::string::npos,
          "new saves did not emit only the explicit Portal-loop schema");
  auto loaded = deserializeWorld(yaml);
  auto const* loadedLoop = loaded.getActiveLayer()->getPortalLoop(loopId);
  require(loadedLoop && loadedLoop->getTraversalOrder().size() == 3 &&
              loadedLoop->getNextEndpointAllocator() == 4,
          "Portal loop identities and allocator did not survive serialization");

  try {
    layer->removePortalEndpoint(loopId, replacementId);
    layer->removePortalEndpoint(loopId, 1);
    require(false, "a two-endpoint Portal loop allowed endpoint deletion");
  } catch (bw::core::CoreException const&) {
  }
}

void activeAperturesCutWallRenderingCollisionAndExposeFallback() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto loopId = layer->addPortalLoop(
      aperture(-50.0f, 0.0f, 16.0f),
      aperture(50.0f, 0.0f, 16.0f));
  auto snapshot = world.getWorldData();
  auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), loopId);
  require(portalLoop && portalLoop->active, "Portal aperture fixture did not resolve");

  auto const* endpoint = snapshot->findPortalEndpoint(
      layer->getId(), loopId, 0);
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
  try {
    bw::core::PortalLoop invalid{0, 18, {bw::core::PortalEndpoint{17, {}}}, {17}};
    require(false, "legacy authored singleton Portal loop was accepted");
  } catch (bw::core::CoreException const&) {
  }
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
  auto loopId = layer->addPortalLoop(
      aperture(-50.0f, 0.0f, 16.0f, 2.0f, 26.0f),
      aperture(0.0f, 50.0f, 16.0f, 10.0f, 34.0f));
  auto snapshot = world.getWorldData();
  auto const* portalLoop = snapshot->findPortalLoop(layer->getId(), loopId);
  require(portalLoop && portalLoop->active, "rigid Portal transform fixture did not resolve");
  require(layer->getPortalLoop(loopId)->getNextEndpointId(0) == 1 &&
              layer->getPortalLoop(loopId)->getNextEndpointId(1) == 0 &&
              bw::core::NextPortalEndpoint(*portalLoop, 0)->endpointId == 1 &&
              bw::core::NextPortalEndpoint(*portalLoop, 1)->endpointId == 0,
          "authored and resolved two-endpoint routes disagree");
  auto reverse = bw::core::BuildPortalMapping(*portalLoop, 1);
  require(reverse.source.centre ==
                  snapshot->findPortalEndpoint(layer->getId(), loopId, 1)
                      ->aperture.centre &&
              reverse.destination.centre ==
                  snapshot->findPortalEndpoint(layer->getId(), loopId, 0)
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
    namedMirrorsRoundTripAndResolveIndependently();
    namedCyclesFollowStableTargets();
    equalAndUnequalWidthsResolveWithoutChangingAuthoredState();
    invalidLoopsStayWholeAndDiagnosable();
    layerSelectionIncludesCompleteLoopsOnly();
    identityAndEndpointStateRoundTripCopyAndAssignment();
    serializedEndpointSequenceCarriesExplicitTraversalOrder();
    orderedLoopsPreserveStableIdentityAndResolveEveryEndpoint();
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
