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

void equalAndUnequalWidthsResolveWithoutChangingAuthoredState() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto const pairId = layer->addPortalPair(
      aperture(-50.0f, 0.0f, 16.0f), aperture(50.0f, 0.0f, 28.0f));

  auto snapshot = world.getWorldData();
  auto const* resolved = snapshot->findPortalPair(layer->getId(), pairId);
  require(resolved && resolved->active,
          "a fully covered Portal pair did not become active");
  require(
      std::abs(resolved->endpoints[0].aperture.width - 16.0f) < 0.001f &&
          std::abs(resolved->endpoints[1].aperture.width - 16.0f) < 0.001f,
      "unequal Portal widths did not resolve to the smaller width");
  require(
      resolved->endpoints[0].aperture.centre == wp::Vector2{-50.0f, 0.0f} &&
          resolved->endpoints[1].aperture.centre == wp::Vector2{50.0f, 0.0f},
      "normalizing Portal widths moved an endpoint centre");
  require(
      layer->getPortalPair(pairId)->getEndpoint(1).getAperture().width ==
          28.0f,
      "Portal resolution mutated the wider authored aperture");
  require(!resolved->endpoints[0].aperture.wallIndices.empty() &&
              !resolved->endpoints[1].aperture.wallIndices.empty(),
          "a resolved aperture did not retain its snapshot wall coverage");
}

void invalidPairsStayWholeAndDiagnosable() {
  auto resolve = [](AuthoredAperture first, AuthoredAperture second) {
    bw::core::World world(200.0f, 10.0f);
    addRoom(world);
    auto* layer = world.getActiveLayer();
    auto const pairId = layer->addPortalPair(first, second);
    auto snapshot = world.getWorldData();
    auto const* pair = snapshot->findPortalPair(layer->getId(), pairId);
    require(pair != nullptr, "selected Portal pair disappeared from generation");
    return *pair;
  };

  auto mismatch = resolve(
      aperture(-50.0f, 0.0f, 16.0f, 0.0f, 24.0f),
      aperture(50.0f, 0.0f, 16.0f, 0.0f, 25.0f));
  require(!mismatch.active && mismatch.endpoints[0].resolved &&
              mismatch.endpoints[1].resolved &&
              mismatch.diagnostic ==
                  PortalResolutionDiagnostic::UnequalEndpointHeights,
          "unequal endpoint heights did not retain resolved bounds and a diagnostic");

  auto missing = resolve(
      aperture(-50.0f, 0.0f), aperture(0.0f, 0.0f));
  require(!missing.active &&
              missing.endpoints[1].diagnostic ==
                  PortalResolutionDiagnostic::MissingRenderedWall &&
              missing.endpoints[0].diagnostic ==
                  PortalResolutionDiagnostic::OtherEndpointUnresolved,
          "a missing partner wall did not deactivate and diagnose the pair");

  auto incomplete = resolve(
      aperture(-50.0f, 0.0f, 120.0f),
      aperture(50.0f, 0.0f, 120.0f));
  require(!incomplete.active &&
              incomplete.diagnostic ==
                  PortalResolutionDiagnostic::IncompleteRenderedWallCoverage,
          "partial wall coverage did not deactivate the pair");

  auto narrow = resolve(
      aperture(-50.0f, 0.0f, 11.0f),
      aperture(50.0f, 0.0f, 16.0f));
  require(!narrow.active &&
              narrow.diagnostic ==
                  PortalResolutionDiagnostic::InsufficientPlayerWidth,
          "an aperture narrower than the player was not diagnosed");

  auto shortPair = resolve(
      aperture(-50.0f, 0.0f, 16.0f, 0.0f, 19.0f),
      aperture(50.0f, 0.0f, 16.0f, 8.0f, 27.0f));
  require(!shortPair.active &&
              shortPair.diagnostic ==
                  PortalResolutionDiagnostic::InsufficientPlayerHeight,
          "an aperture shorter than the player was not diagnosed");
}

void layerSelectionIncludesCompletePairsOnly() {
  bw::core::World world(200.0f, 10.0f);
  auto* first = world.getActiveLayer();
  addRoom(world);
  auto const pairId = first->addPortalPair(
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  auto* second = world.addLayer("Second");

  auto* generator = world.getWorldDataGenerator();
  generator->setLayerSelection(bw::core::SelectLayer(second->getId()));
  auto absent = world.getWorldData();
  require(absent->findPortalPair(first->getId(), pairId) == nullptr,
          "a Portal pair participated while its owning Layer was unselected");

  generator->setLayerSelection(bw::core::SelectLayer(first->getId()));
  auto present = world.getWorldData();
  auto const* resolved = present->findPortalPair(first->getId(), pairId);
  require(resolved && resolved->active,
          "selecting a Portal's Layer did not include both endpoints");

  // Geometry only on an unselected Layer cannot satisfy either endpoint.
  bw::core::World split(200.0f, 10.0f);
  auto* portalLayer = split.getActiveLayer();
  auto const splitPair = portalLayer->addPortalPair(
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  auto* geometryLayer = split.addLayer("Geometry");
  split.setActiveLayer(geometryLayer);
  addRoom(split);
  split.getWorldDataGenerator()->setLayerSelection(
      bw::core::SelectLayer(portalLayer->getId()));
  auto noGeometry = split.getWorldData();
  auto const* inactive =
      noGeometry->findPortalPair(portalLayer->getId(), splitPair);
  require(inactive && !inactive->active,
          "Portal resolution used geometry absent from the selected generation");
}

void identityAndEndpointStateRoundTripCopyAndAssignment() {
  bw::core::World source(200.0f, 10.0f);
  addRoom(source);
  auto* layer = source.getActiveLayer();
  auto removed = layer->addPortalPair(
      aperture(-50.0f, -20.0f), aperture(50.0f, -20.0f));
  layer->removePortalPair(removed);
  auto const pairId = layer->addPortalPair(
      aperture(-50.0f, 7.0f, 18.0f, 3.0f, 27.0f),
      aperture(50.0f, -9.0f, 22.0f, 5.0f, 29.0f));

  bw::core::World copied(source);
  bw::core::World assigned(100.0f, 10.0f);
  assigned = source;
  auto loaded = deserializeWorld(serializeWorld(source));
  auto binaryLoaded = binaryRoundTrip(source);
  for (auto const* candidate : {&copied, &assigned, &loaded, &binaryLoaded}) {
    auto const* pair = candidate->getActiveLayer()->getPortalPair(pairId);
    require(pair && pair->getId() == pairId,
            "Portal pair identity did not survive a value operation");
    auto const& first = pair->getEndpoint(0).getAperture();
    auto const& second = pair->getEndpoint(1).getAperture();
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

  bw::core::World deletedPairWorld(200.0f, 10.0f);
  auto* deletedPairLayer = deletedPairWorld.getActiveLayer();
  auto const deletedId = deletedPairLayer->addPortalPair(
      aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f));
  deletedPairLayer->removePortalPair(deletedId);
  auto yamlAfterDelete = deserializeWorld(serializeWorld(deletedPairWorld));
  auto binaryAfterDelete = binaryRoundTrip(deletedPairWorld);
  require(yamlAfterDelete.getActiveLayer()->addPortalPair(
              aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f)) == 1 &&
              binaryAfterDelete.getActiveLayer()->addPortalPair(
                  aperture(-50.0f, 0.0f), aperture(50.0f, 0.0f)) == 1,
          "reload reused a deleted Portal pair's stable id");
}

void activeAperturesCutWallRenderingCollisionAndExposeFallback() {
  bw::core::World world(200.0f, 10.0f);
  addRoom(world);
  auto* layer = world.getActiveLayer();
  auto pairId = layer->addPortalPair(
      aperture(-50.0f, 0.0f, 16.0f),
      aperture(50.0f, 0.0f, 16.0f));
  auto snapshot = world.getWorldData();
  auto const* pair = snapshot->findPortalPair(layer->getId(), pairId);
  require(pair && pair->active, "Portal aperture fixture did not resolve");

  auto wallIndex = pair->endpoints[0].aperture.wallIndices.front();
  auto segments = snapshot->getWallCollisionSegments(wallIndex);
  require(segments.size() == 2,
          "active aperture did not split its source wall collision span");
  auto const& opening = pair->endpoints[0].aperture;
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
  std::array<uint32_t, 2> pairOrder{17, 42};
  require(bw::core::NextPortalEndpointId(pairOrder, 17) == 42 &&
              bw::core::NextPortalEndpointId(pairOrder, 42) == 17,
          "two-endpoint routing must work in both directions");
  bw::core::ResolvedPortalPair pair;
  pair.endpoints[0].endpointId = 17;
  pair.endpoints[1].endpointId = 42;
  require(bw::core::FindPortalEndpoint(pair, 42) == &pair.endpoints[1] &&
              bw::core::NextPortalEndpoint(pair, 17) == &pair.endpoints[1] &&
              bw::core::NextPortalEndpoint(pair, 42) == &pair.endpoints[0] &&
              !bw::core::FindPortalEndpoint(pair, 0) &&
              !bw::core::NextPortalEndpoint(pair, 0),
          "resolved Portal routing confused stable identity with array position");
  try {
    [[maybe_unused]] auto next = bw::core::NextPortalEndpointId(order, 99);
    require(false, "unknown endpoint ID was accepted");
  } catch (bw::core::CoreException const&) {
    // A stale ID must not silently select a different endpoint.
  }
  std::array<uint32_t, 1> singleton{17};
  try {
    [[maybe_unused]] auto next = bw::core::NextPortalEndpointId(singleton, 17);
    require(false, "singleton Portal loop was accepted");
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
  auto pairId = layer->addPortalPair(
      aperture(-50.0f, 0.0f, 16.0f, 2.0f, 26.0f),
      aperture(0.0f, 50.0f, 16.0f, 10.0f, 34.0f));
  auto snapshot = world.getWorldData();
  auto const* pair = snapshot->findPortalPair(layer->getId(), pairId);
  require(pair && pair->active, "rigid Portal transform fixture did not resolve");
  require(layer->getPortalPair(pairId)->getNextEndpointId(0) == 1 &&
              layer->getPortalPair(pairId)->getNextEndpointId(1) == 0 &&
              bw::core::NextPortalEndpointIndex(*pair, 0) == 1 &&
              bw::core::NextPortalEndpointIndex(*pair, 1) == 0,
          "authored and resolved two-endpoint routes disagree");
  auto reverse = bw::core::BuildPortalRigidTransform(*pair, 1);
  require(reverse.source.centre == pair->endpoints[1].aperture.centre &&
              reverse.destination.centre == pair->endpoints[0].aperture.centre,
          "reverse route did not exit through the first endpoint");
  auto transform = bw::core::BuildPortalRigidTransform(*pair, 0);
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
  require(yaml.find("portalPairs") == std::string::npos &&
              yaml.find("nextPortalPairId") == std::string::npos,
          "an empty Portal collection changed keyed World serialization");
  auto loaded = deserializeWorld(yaml);
  auto after = loaded.getWorldData();
  require(after->getPortalPairs().empty() &&
              before->getTriangles().size() == after->getTriangles().size() &&
              before->getWalls().size() == after->getWalls().size(),
          "a no-Portal World changed generated geometry after reload");
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    equalAndUnequalWidthsResolveWithoutChangingAuthoredState();
    invalidPairsStayWholeAndDiagnosable();
    layerSelectionIncludesCompletePairsOnly();
    identityAndEndpointStateRoundTripCopyAndAssignment();
    activeAperturesCutWallRenderingCollisionAndExposeFallback();
    portalCentresSnapOnlyToNearestLegalWallCoverage();
    canonicalNextEndpointRoutesByStableIdentity();
    canonicalRigidTransformPreservesScaleAndWorldUp();
    noPortalWorldKeepsItsKeyedSerializationShapeAndGeometry();
    std::cout << "Portal pair authoring and resolution passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
