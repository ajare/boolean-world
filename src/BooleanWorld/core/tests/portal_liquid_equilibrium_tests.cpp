#include "PortalTestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/ArrangementWorldData.h>
#include <core/LayerBuildStep.h>
#include <core/MeshPrimitive.h>
#include <core/Portal.h>
#include <core/World.h>

namespace {
using bw::core::ArrangementWorldDataPtr;
using bw::core::AuthoredAperture;
using bw::core::MeshPrimitive;
using bw::core::PortalLiquidDiagnostic;
using bw::core::Primitive;
using bw::core::World;

constexpr double Epsilon = 0.001;

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(double actual, double expected, std::string const& message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) >= Epsilon) {
    throw std::runtime_error(
        message + " (expected " + std::to_string(expected) + ", got " +
        std::to_string(actual) + ")");
  }
}

MeshPrimitive* addRoom(
    World& world, wp::Vector2 centre, float floor, float ceiling,
    float liquidLevel = 0.0f) {
  bw::core::ComplexPolygon square{
      {{{-25.0f, -25.0f}}, {{25.0f, -25.0f}}, {{25.0f, 25.0f}}, {{-25.0f, 25.0f}}}};
  auto* room = MeshPrimitive::fromComplexPolygons(
      Primitive::Operation::Union, {square});
  room->setPosition(centre);
  room->updateVertexPositions();
  auto properties = room->getProperties();
  properties.floorZ = floor;
  properties.ceilingZ = ceiling;
  properties.liquidLevel = liquidLevel;
  room->setProperties(properties);
  world.addPrimitive(room);
  return room;
}

AuthoredAperture aperture(
    float x, float y, float bottom, float width = 16.0f) {
  return {{x, y}, width, bottom, bottom + 24.0f};
}

struct TwoRoomResult {
  ArrangementWorldDataPtr data;
  uint32_t cyclePortalId{};
};

TwoRoomResult twoRooms(
    float sourceLiquidLevel, float width = 16.0f,
    bool destinationDrain = false, float destinationLiquidLevel = 0.0f,
    float destinationCeiling = 52.0f) {
  World world(300.0f, 10.0f);
  addRoom(world, {-75.0f, 0.0f}, 0.0f, 40.0f, sourceLiquidLevel);
  auto* destination = addRoom(
      world, {75.0f, 0.0f}, 12.0f, destinationCeiling,
      destinationLiquidLevel);
  if (destinationDrain) {
    auto proxy = destination->createEditingProxy();
    auto edge = proxy->getFirstEdgeIndex();
    require(proxy->setEdgeCollisionOverride(edge, false),
            "the destination drain edge could not be opened");
    proxy->commitTo(*destination);
  }
  auto* layer = world.getActiveLayer();
  auto firstPortalId = bw::test::addPortalCycle(layer,
      aperture(-50.0f, 0.0f, 5.0f, width),
      aperture(50.0f, 0.0f, 15.0f, width));
  return {world.getWorldData(), firstPortalId};
}

void waterBlockingIsDirectionalAndRegenerates() {
  for (bool reverse : {false, true}) {
    World world(300.0f, 10.0f);
    addRoom(world, {-75, 0}, 0, 40, reverse ? 0 : 20);
    addRoom(world, {75, 0}, 0, 40, reverse ? 20 : 0);
    auto* layer = world.getActiveLayer();
    auto a = bw::test::addPortalCycle(layer,
        aperture(-50, 0, 5), aperture(50, 0, 5));
    auto b = layer->getPortal(a)->getTargetId();
    auto original = world.getWorldData();
    layer->setPortalBlocksWater(a, true);
    auto blocked = world.getWorldData();
    require(blocked->getPortalLoops().front().active &&
                blocked->getPortalLiquidDiagnostics().empty() &&
                blocked->getPortalLiquidAdjacency().size() == 1 &&
                blocked->getPortalLiquidAdjacency().front().sourceEndpointId == b,
            "blocking a source removed incoming transport or deactivated the loop");
    requireNear(blocked->getLiquidDepth({-75, 0}), reverse ? 10 : 20,
                "blocked source lost water or refused incoming water");
    requireNear(blocked->getLiquidDepth({75, 0}), reverse ? 10 : 0,
                "directional blocking produced incorrect destination water");
    require(original->getPortalLiquidAdjacency().size() == 2,
            "changing authored flag mutated a previous snapshot");
    layer->setPortalBlocksWater(b, true);
    auto sealed = world.getWorldData();
    require(sealed->getPortalLiquidAdjacency().empty() &&
                sealed->getPortalLiquidDiagnostics().empty(),
            "fully blocked pair retained transport or emitted a diagnostic");
    requireNear(sealed->getLiquidDepth({-75, 0}), reverse ? 0 : 20,
                "regeneration did not restore authored water inputs");
    layer->setPortalBlocksWater(a, false);
    layer->setPortalBlocksWater(b, false);
    require(world.getWorldData()->getLiquidPoolElevations() ==
                original->getLiquidPoolElevations(),
            "clearing flags did not restore original settlement");
    layer->setPortalTarget(a, a);
    layer->setPortalTarget(b, b);
    for (bool flag : {false, true}) {
      layer->setPortalBlocksWater(a, flag);
      require(world.getWorldData()->getPortalLiquidAdjacency().empty(),
              "mirror transported Liquid");
    }
  }
}

void portalAdjacencyIsSeparateAndWaitsForItsSill() {
  auto below = twoRooms(4.0f);
  require(below.data->getPortalLiquidAdjacency().size() == 2,
          "an active resolved aperture did not expose portal liquid-adjacency");
  auto const& link = below.data->getPortalLiquidAdjacency().front();
  require(link.cyclePortalId == below.cyclePortalId &&
              link.sourceEndpointId == 0 &&
              link.destinationEndpointId == 1 &&
              link.face0 != link.face1 && link.sill0 == 5.0 &&
              link.sill1 == 15.0 && link.elevationOffset == 10.0,
          "portal liquid-adjacency lost its faces, Sills, or elevation mapping");

  auto ordinary = bw::core::arr::BuildLiquidAdjacency(
      below.data->getArrangement());
  require(std::ranges::none_of(ordinary, [&](auto const& adjacency) {
            return !adjacency.drain &&
                   ((adjacency.face0 == link.face0 &&
                     adjacency.face1 == link.face1) ||
                    (adjacency.face0 == link.face1 &&
                     adjacency.face1 == link.face0));
          }),
          "portal liquid-adjacency leaked into ordinary shared-edge adjacency");
  requireNear(below.data->getLiquidDepth({-75.0f, 0.0f}), 4.0,
              "Liquid below the source Portal Sill moved");
  requireNear(below.data->getLiquidDepth({75.0f, 0.0f}), 0.0,
              "a dry destination filled before the source reached its Portal Sill");
}

void differingFloorsMapRelativeElevationAndConserveVolume() {
  auto settled = twoRooms(20.0f);
  require(settled.data->getPortalLiquidDiagnostics().empty(),
          "a valid Portal Liquid connection emitted a diagnostic");
  requireNear(settled.data->getLiquidDepth({-75.0f, 0.0f}), 11.0,
              "the source Pool did not settle at the conserved relative equilibrium");
  requireNear(settled.data->getLiquidDepth({75.0f, 0.0f}), 9.0,
              "the dry destination did not receive the conserved Portal volume");
  requireNear(settled.data->getLiquidSurfaceHeight({-75.0f, 0.0f}), 11.0,
              "the source surface elevation was incorrect");
  requireNear(settled.data->getLiquidSurfaceHeight({75.0f, 0.0f}), 21.0,
              "the destination surface was not mapped relative to its endpoint bottom");

  double volume = 0.0;
  auto const& cells = settled.data->getHydraulicCells();
  auto const& elevations = settled.data->getLiquidPoolElevations();
  for (size_t cell = 0; cell < cells.size(); ++cell) {
    if (std::isfinite(elevations[cell])) {
      volume += cells[cell].volumeBelow(elevations[cell]);
    }
  }
  requireNear(volume, 20.0 * 50.0 * 50.0,
              "Portal equilibrium created or destroyed Liquid volume");

  auto sourceFace = settled.data->getContainingFaceIndex({-75.0f, 0.0f});
  auto destinationFace =
      settled.data->getContainingFaceIndex({75.0f, 0.0f});
  auto const& surfaces = settled.data->getLiquidSurfaceTriangles();
  require(std::ranges::any_of(surfaces, [&](auto const& triangle) {
            return triangle.face == sourceFace;
          }) &&
              std::ranges::any_of(surfaces, [&](auto const& triangle) {
                return triangle.face == destinationFace;
              }),
          "the existing Liquid surface output did not contain both Portal-linked faces");

  auto reverse = twoRooms(0.0f, 16.0f, false, 20.0f);
  requireNear(reverse.data->getLiquidDepth({-75.0f, 0.0f}), 11.0,
              "Liquid did not pass backward through the Portal loop");
  requireNear(reverse.data->getLiquidDepth({75.0f, 0.0f}), 9.0,
              "reverse Portal flow did not conserve its destination seed volume");

  auto capped = twoRooms(60.0f, 16.0f, false, 0.0f, 39.0f);
  requireNear(capped.data->getLiquidDepth({-75.0f, 0.0f}), 33.0,
              "destination ceiling capacity did not return excess volume to the source Pool");
  requireNear(capped.data->getLiquidDepth({75.0f, 0.0f}), 27.0,
              "Portal equilibrium exceeded the destination ceiling capacity");
}

void widthDoesNotChangeInstantaneousEquilibriumAndDrainsStillWork() {
  auto narrow = twoRooms(20.0f, 16.0f);
  auto wide = twoRooms(20.0f, 30.0f);
  requireNear(
      narrow.data->getLiquidDepth({-75.0f, 0.0f}),
      wide.data->getLiquidDepth({-75.0f, 0.0f}),
      "resolved width changed the source equilibrium");
  requireNear(
      narrow.data->getLiquidDepth({75.0f, 0.0f}),
      wide.data->getLiquidDepth({75.0f, 0.0f}),
      "resolved width changed the destination equilibrium");
  requireNear(
      narrow.data->getPortalLiquidAdjacency().front().resolvedWidth, 16.0,
      "the narrow resolved width was not retained as connectivity data");
  requireNear(
      wide.data->getPortalLiquidAdjacency().front().resolvedWidth, 30.0,
      "the wide resolved width was not retained as connectivity data");

  auto drained = twoRooms(20.0f, 16.0f, true);
  requireNear(drained.data->getLiquidDepth({-75.0f, 0.0f}), 0.0,
              "a Portal-connected source did not empty through an ordinary drain");
  requireNear(drained.data->getLiquidDepth({75.0f, 0.0f}), 0.0,
              "the ordinary drain retained Portal-transmitted Liquid");
}

struct CycleResult {
  ArrangementWorldDataPtr data;
  uint32_t closingLoop{};
};

CycleResult portalCycle(bool contradictory) {
  World world(400.0f, 10.0f);
  addRoom(world, {-100.0f, 0.0f}, 0.0f, 40.0f, 30.0f);
  addRoom(world, {0.0f, 0.0f}, 10.0f, 50.0f);
  addRoom(world, {100.0f, 0.0f}, 20.0f, 60.0f);
  auto* layer = world.getActiveLayer();
  [[maybe_unused]] auto firstLoop = bw::test::addPortalCycle(layer,
      aperture(-75.0f, 0.0f, 5.0f),
      aperture(-25.0f, 0.0f, 15.0f));
  [[maybe_unused]] auto secondLoop = bw::test::addPortalCycle(layer,
      aperture(25.0f, 0.0f, 15.0f),
      aperture(75.0f, 0.0f, 25.0f));
  auto closingLoop = bw::test::addPortalCycle(layer,
      aperture(125.0f, 0.0f, 25.0f),
      aperture(-125.0f, 0.0f, contradictory ? 6.0f : 5.0f));
  return {world.getWorldData(), closingLoop};
}

void consistentAndContradictoryCyclesAreSettledDeterministically() {
  auto consistent = portalCycle(false);
  require(consistent.data->getPortalLiquidDiagnostics().empty(),
          "a consistent accumulated elevation cycle was rejected");
  requireNear(consistent.data->getLiquidDepth({-100.0f, 0.0f}), 10.0,
              "the consistent cycle did not settle its source once");
  requireNear(consistent.data->getLiquidDepth({0.0f, 0.0f}), 10.0,
              "the consistent cycle did not settle its middle Pool once");
  requireNear(consistent.data->getLiquidDepth({100.0f, 0.0f}), 10.0,
              "the consistent cycle did not settle its destination once");

  auto contradictory = portalCycle(true);
  require(contradictory.data->getPortalLiquidDiagnostics().size() == 1,
          "a contradictory accumulated elevation cycle lacked one deterministic diagnostic");
  auto const& diagnostic =
      contradictory.data->getPortalLiquidDiagnostics().front();
  require(diagnostic.cyclePortalId == contradictory.closingLoop &&
              diagnostic.diagnostic ==
                  PortalLiquidDiagnostic::ContradictoryElevationCycle,
          "the contradictory cycle diagnostic did not identify the conflicting loop");
  require(std::ranges::none_of(
              contradictory.data->getPortalLiquidAdjacency(),
              [&](auto const& adjacency) {
                return adjacency.cyclePortalId == contradictory.closingLoop;
              }),
          "the conflicting portal liquid-adjacency was not excluded");
  requireNear(contradictory.data->getLiquidDepth({-100.0f, 0.0f}), 10.0,
              "excluding the conflicting edge changed the valid chain equilibrium");
  requireNear(contradictory.data->getLiquidDepth({0.0f, 0.0f}), 10.0,
              "excluding the conflicting edge changed the middle Pool equilibrium");
  requireNear(contradictory.data->getLiquidDepth({100.0f, 0.0f}), 10.0,
              "excluding the conflicting edge changed the destination equilibrium");

  auto rebuilt = portalCycle(true);
  require(rebuilt.data->getLiquidDepths() ==
                  contradictory.data->getLiquidDepths() &&
              rebuilt.data->getLiquidPoolElevations() ==
                  contradictory.data->getLiquidPoolElevations() &&
              rebuilt.data->getPortalLiquidDiagnostics().size() == 1 &&
              rebuilt.data->getPortalLiquidDiagnostics().front().cyclePortalId ==
                  diagnostic.cyclePortalId &&
              rebuilt.data->getPortalLiquidDiagnostics().front().diagnostic ==
                  diagnostic.diagnostic,
          "rebuilding an unchanged Portal cycle changed Liquid or diagnostics");
}
double liquidVolume(ArrangementWorldDataPtr const& data) {
  double volume = 0.0;
  auto const& cells = data->getHydraulicCells();
  auto const& elevations = data->getLiquidPoolElevations();
  for (size_t cell = 0; cell < cells.size(); ++cell) {
    if (std::isfinite(elevations[cell])) {
      volume += cells[cell].volumeBelow(elevations[cell]);
    }
  }
  return volume;
}

ArrangementWorldDataPtr directedRooms(
    float seed, float intermediateFloor, bool reverse = false,
    bool drain = false, bool blockMiddle = false) {
  World world(400.0f, 10.0f);
  addRoom(world, {-100.0f, 0.0f}, 0.0f, 50.0f, seed);
  auto* middle = addRoom(world, {0.0f, 0.0f}, intermediateFloor, 60.0f);
  addRoom(world, {100.0f, 0.0f}, 20.0f, 70.0f);
  if (drain) {
    auto proxy = middle->createEditingProxy();
    require(proxy->setEdgeCollisionOverride(proxy->getFirstEdgeIndex(), false),
            "could not open intermediate ordinary drain");
    proxy->commitTo(*middle);
  }
  auto* layer = world.getActiveLayer();
  auto first = layer->addPortal(aperture(-75.0f, 0.0f, 5.0f));
  auto second = layer->addPortal(aperture(-25.0f, 0.0f, 15.0f));
  auto third = layer->addPortal(aperture(75.0f, 0.0f, 25.0f));
  layer->setPortalTarget(first, reverse ? third : second);
  layer->setPortalTarget(second, reverse ? first : third);
  layer->setPortalTarget(third, reverse ? second : first);
  layer->setPortalBlocksWater(second, blockMiddle);
  auto data = world.getWorldData();
  require(data->getPortalLiquidDiagnostics().empty(),
          "directed loop unexpectedly failed Liquid validation");
  require(data->getPortalLiquidAdjacency().size() == (blockMiddle ? 2 : 3),
          "a three-endpoint loop must omit only blocked outgoing hops");
  auto const& resolved = data->getPortalLoops().front();
  for (auto const& hop : data->getPortalLiquidAdjacency()) {
    require(hop.destinationEndpointId == bw::core::NextPortalEndpointId(
                                             resolved.traversalOrder, hop.sourceEndpointId),
            "Liquid adjacency did not follow canonical traversal order");
    require(hop.resolvedWidth == 16.0f &&
                hop.sill1 - hop.sill0 == hop.elevationOffset &&
                data->getHydraulicCells()[hop.cell0].triangle.face == hop.face0 &&
                data->getHydraulicCells()[hop.cell1].triangle.face == hop.face1,
            "directed hop lost its generated geometry");
    require(std::ranges::none_of(data->getPortalLiquidAdjacency(),
                                 [&](auto const& other) {
                                   return other.sourceEndpointId == hop.destinationEndpointId &&
                                          other.destinationEndpointId == hop.sourceEndpointId;
                                 }),
            "a longer loop acquired an implicit reverse hop");
  }
  require(third == 2, "unexpected endpoint allocator state");
  return data;
}

void directedSpillCannotSkipAnIntermediateSill() {
  // Canonical Sills are all 5. B's deeper floor absorbs the entire spill
  // without reaching its outgoing Sill. An undirected A--C shortcut would
  // incorrectly wet C, and merging A with B would sink A below its Sill.
  auto forward = directedRooms(20.0f, -10.0f);
  requireNear(forward->getLiquidDepth({-100.0f, 0.0f}), 5.0,
              "the directed donor did not retain its Sill volume");
  requireNear(forward->getLiquidDepth({0.0f, 0.0f}), 15.0,
              "the next endpoint did not receive the complete spill");
  requireNear(forward->getLiquidDepth({100.0f, 0.0f}), 0.0,
              "Liquid bypassed the intermediate source Sill");
  requireNear(liquidVolume(forward), 20.0 * 2500.0,
              "directed spill did not conserve volume");

  auto reversed = directedRooms(20.0f, -10.0f, true);
  requireNear(reversed->getLiquidDepth({-100.0f, 0.0f}), 5.0,
              "reordered source did not spill to its Sill");
  requireNear(reversed->getLiquidDepth({100.0f, 0.0f}), 5.0,
              "reordered intermediate endpoint did not retain its Sill volume");
  requireNear(reversed->getLiquidDepth({0.0f, 0.0f}), 10.0,
              "reordered loop did not spill into the final endpoint");
  requireNear(liquidVolume(reversed), 20.0 * 2500.0,
              "reordered loop did not conserve volume");

  auto below = directedRooms(4.0f, -10.0f);
  requireNear(below->getLiquidDepth({-100.0f, 0.0f}), 4.0,
              "Liquid moved before reaching the source Sill");
  requireNear(below->getLiquidDepth({0.0f, 0.0f}), 0.0,
              "a below-Sill source wetted its successor");
  auto rebuilt = directedRooms(20.0f, -10.0f);
  require(forward->getLiquidPoolElevations() == rebuilt->getLiquidPoolElevations(),
          "directed settlement changed on an identical rebuild");
}

void directedLoopsReachEquilibriumAndOrdinaryDrains() {
  auto blockedMiddle = directedRooms(30.0f, 10.0f, false, false, true);
  requireNear(blockedMiddle->getLiquidDepth({100, 0}), 0,
              "Liquid crossed a blocked intermediate Portal");
  requireNear(liquidVolume(blockedMiddle), 30.0 * 2500.0,
              "blocked longer cycle lost volume");
  require(blockedMiddle->getLiquidDepth({0, 0}) > 0,
          "blocked intermediate Portal refused incoming Liquid");
  auto filled = directedRooms(60.0f, 10.0f);
  for (auto x : {-100.0f, 0.0f, 100.0f}) {
    requireNear(filled->getLiquidDepth({x, 0.0f}), 20.0,
                "a fully reached loop failed to reach relative equilibrium");
  }
  requireNear(liquidVolume(filled), 60.0 * 2500.0,
              "a fully reached loop lost volume");
  auto drained = directedRooms(20.0f, -10.0f, false, true);
  requireNear(liquidVolume(drained), 0.0,
              "a directed source did not empty into an ordinary drain");
  auto blocked = directedRooms(4.0f, -10.0f, false, true);
  requireNear(liquidVolume(blocked), 4.0 * 2500.0,
              "a drain crossed an unreached source Sill");
}

void directedSpillIntegratesAffineCapacity() {
  World world(400.0f, 10.0f);
  auto* source = addRoom(world, {-100.0f, 0.0f}, 0.0f, 50.0f, 20.0f);
  auto properties = source->getProperties();
  properties.floorSpan = {0.0f, 0.0f, 10.0f};
  properties.floorSpanAuthored = true;
  source->setProperties(properties);
  addRoom(world, {0.0f, 0.0f}, -20.0f, 50.0f);
  addRoom(world, {100.0f, 0.0f}, 0.0f, 50.0f);
  auto* layer = world.getActiveLayer();
  auto loop = bw::test::addPortalCycle(layer,
      aperture(-75.0f, 0.0f, 7.0f), aperture(-25.0f, 0.0f, 7.0f));
  [[maybe_unused]] auto third = bw::test::insertPortalAfter(layer, layer->getPortal(loop)->getTargetId(), aperture(75.0f, 0.0f, 7.0f));
  auto data = world.getWorldData();
  require(data->getPortalLiquidAdjacency().size() == 3,
          "sloped source aperture did not resolve into directed hops");
  requireNear(data->getLiquidDepth({-100.0f, 0.0f}), 2.0,
              "sloped source did not retain a horizontal surface at its Sill");
  requireNear(data->getLiquidDepth({-100.0f, 20.0f}), 0.0,
              "directed spill did not expose the sloped shoreline");
  // V(7) = area * 7^2 / (2 * 10) = 6125, leaving 43875 to spill.
  requireNear(data->getLiquidDepth({0.0f, 0.0f}), 17.55,
              "directed spill did not integrate affine donor capacity");
  requireNear(data->getLiquidDepth({100.0f, 0.0f}), 0.0,
              "affine spill bypassed the intermediate Sill");
  requireNear(liquidVolume(data), 20.0 * 2500.0,
              "directed affine settlement did not conserve volume");
}

void floodedIntermediateCellsRemainDirectedConduits() {
  for (auto sourceCeiling : {100.0f, 29.0f}) {
    for (auto seed : {160.0f, 500.0f}) {
      World world(500.0f, 10.0f);
      addRoom(world, {-150.0f, 0.0f}, 0.0f, sourceCeiling, seed);
      addRoom(world, {-50.0f, 0.0f}, 0.0f, 29.0f);
      addRoom(world, {50.0f, 0.0f}, 0.0f, 29.0f);
      addRoom(world, {150.0f, 0.0f}, 0.0f, 100.0f);
      auto* layer = world.getActiveLayer();
      auto loop = bw::test::addPortalCycle(layer,
          aperture(-125.0f, 0.0f, 5.0f), aperture(-25.0f, 0.0f, 5.0f));
      auto third = bw::test::insertPortalAfter(layer, layer->getPortal(loop)->getTargetId(), aperture(75.0f, 0.0f, 5.0f));
      [[maybe_unused]] auto fourth = bw::test::insertPortalAfter(layer, third, aperture(175.0f, 0.0f, 5.0f));
      auto data = world.getWorldData();
      require(data->getPortalLiquidAdjacency().size() == 4 &&
                  data->getPortalLiquidDiagnostics().empty(),
              "a four-endpoint loop did not expose all directed hops");
      requireNear(data->getLiquidDepth({-150.0f, 0.0f}),
                  seed == 500.0f ? sourceCeiling : (sourceCeiling == 100.0f ? 51.0 : 29.0),
                  "source did not distribute volume through flooded cells");
      for (auto x : {-50.0f, 50.0f}) {
        requireNear(data->getLiquidDepth({x, 0.0f}), 29.0,
                    "intermediate capacity was exceeded");
      }
      requireNear(data->getLiquidDepth({150.0f, 0.0f}),
                  seed == 500.0f ? 100.0 : (sourceCeiling == 100.0f ? 51.0 : 73.0),
                  "flooded intermediate ceilings became false dams");
      requireNear(liquidVolume(data),
                  std::min(seed, sourceCeiling + 158.0f) * 2500.0,
                  "a flooded directed conduit discarded reachable volume");
    }
  }
}

void lateLoopConflictsAreAtomicAndLiquidOnly() {
  World world(400.0f, 10.0f);
  addRoom(world, {-100.0f, 0.0f}, 0.0f, 50.0f, 20.0f);
  addRoom(world, {0.0f, 0.0f}, 10.0f, 60.0f);
  addRoom(world, {100.0f, 0.0f}, 20.0f, 70.0f);
  auto* layer = world.getActiveLayer();
  auto accepted = bw::test::addPortalCycle(layer,
      aperture(-75.0f, 0.0f, 5.0f), aperture(-25.0f, 0.0f, 15.0f));
  auto rejected = bw::test::addPortalCycle(layer,
      aperture(25.0f, 0.0f, 15.0f), aperture(75.0f, 0.0f, 25.0f));
  auto last = bw::test::insertPortalAfter(layer, layer->getPortal(rejected)->getTargetId(), aperture(-125.0f, 0.0f, 6.0f));
  auto data = world.getWorldData();
  require(data->getPortalLiquidDiagnostics().size() == 1 &&
              data->getPortalLiquidDiagnostics().front().cyclePortalId == rejected &&
              data->getPortalLiquidDiagnostics().front().diagnostic ==
                  PortalLiquidDiagnostic::ContradictoryElevationCycle,
          "a late offset conflict did not produce one loop diagnostic");
  require(data->getPortalLiquidAdjacency().size() == 2 &&
              std::ranges::all_of(data->getPortalLiquidAdjacency(),
                                  [&](auto const& hop) { return hop.cyclePortalId == accepted; }),
          "a rejected loop leaked its earlier valid hop");
  auto const* loop = data->findPortalLoop(layer->getId(), rejected);
  require(loop && loop->active && loop->endpoints.size() == 3,
          "Liquid-only rejection deactivated the generated Portal loop");
  for (auto const& endpoint : loop->endpoints) {
    auto const& opening = endpoint.aperture;
    require(data->circleIntersectsWall(opening.centre, 2.0f) < 0,
            "Liquid-only rejection restored collision inside an aperture");
    auto wall = opening.wallIndices.front();
    auto replacements = data->getDetail().replacementsFor(
        bw::core::arr::DetailSurfaceKind::Wall, wall);
    require(std::ranges::count_if(replacements, [](auto const& triangle) {
              return triangle.kind ==
                     bw::core::arr::DetailTriangleKind::PortalFallback;
            }) == 2,
            "Liquid-only rejection removed Portal rendering geometry");
  }
  auto transform = bw::core::BuildPortalMapping(*loop, last);
  requireNear(transform.transformElevation(6.0f), 15.0,
              "Liquid rejection changed player/render/Torch routing");
  requireNear(data->getLiquidDepth({100.0f, 0.0f}), 0.0,
              "a rejected loop changed settlement through an early hop");
  requireNear(liquidVolume(data), 20.0 * 2500.0,
              "atomic rejection lost Liquid volume");

  // Exercise missing incident cells at a late endpoint with generated
  // geometry: omit its incident cells from the hydraulic input only.
  auto cells = data->getHydraulicCells();
  auto missingFace = data->getContainingFaceIndex({-100.0f, 0.0f});
  std::erase_if(cells, [&](auto const& cell) {
    return cell.triangle.face == missingFace;
  });
  auto missing = bw::core::BuildPortalLiquidAdjacency(
      data->getArrangement(), data->getWalls(), cells, {*loop});
  require(missing.diagnostics.size() == 1 &&
              missing.diagnostics.front().cyclePortalId == rejected &&
              missing.diagnostics.front().diagnostic ==
                  PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint &&
              std::ranges::none_of(missing.adjacency,
                                   [&](auto const& hop) { return hop.cyclePortalId == rejected; }),
          "a missing incident cell did not reject the entire loop atomically");

  // This offset deliberately disagrees with the rejected loop's first
  // tentative hop; leaking its constraints would reject this valid loop too.
  auto later = bw::test::addPortalCycle(layer,
      aperture(25.0f, 0.0f, 15.0f), aperture(75.0f, 0.0f, 26.0f));
  auto after = world.getWorldData();
  require(after->getPortalLiquidDiagnostics().size() == 1 &&
              after->getPortalLiquidAdjacency().size() == 4 &&
              std::ranges::any_of(after->getPortalLiquidAdjacency(),
                                  [&](auto const& hop) { return hop.cyclePortalId == later; }),
          "a failed trial poisoned the constraints for a later valid loop");
  requireNear(liquidVolume(after), 20.0 * 2500.0,
              "a later accepted loop lost Liquid volume");

  auto shuffled = after->getPortalLoops();
  std::ranges::reverse(shuffled);
  auto reordered = bw::core::BuildPortalLiquidAdjacency(
      after->getArrangement(), after->getWalls(), after->getHydraulicCells(),
      shuffled);
  require(reordered.diagnostics.size() == 1 &&
              reordered.diagnostics.front().cyclePortalId == rejected &&
              reordered.adjacency.size() == after->getPortalLiquidAdjacency().size(),
          "trial order depended on snapshot vector order rather than stable IDs");
  for (size_t index = 0; index < reordered.adjacency.size(); ++index) {
    auto const& actual = reordered.adjacency[index];
    auto const& expected = after->getPortalLiquidAdjacency()[index];
    require(actual.cyclePortalId == expected.cyclePortalId &&
                actual.sourceEndpointId == expected.sourceEndpointId &&
                actual.cell0 == expected.cell0 && actual.cell1 == expected.cell1,
            "stable loop order did not produce stable directed hops");
  }

  // Blocking the two conflicting outgoing hops leaves the first hop valid.
  // Their constraints and missing-cell checks must not poison enabled hops.
  layer->setPortalBlocksWater(layer->getPortal(rejected)->getTargetId(), true);
  layer->setPortalBlocksWater(last, true);
  layer->setPortalBlocksWater(later, true);
  layer->setPortalBlocksWater(layer->getPortal(later)->getTargetId(), true);
  auto unblockedTrial = world.getWorldData();
  require(unblockedTrial->getPortalLiquidDiagnostics().empty() &&
              unblockedTrial->getPortalLiquidAdjacency().size() == 3,
          "blocked hops retained contradictory elevation constraints");
  auto blockedLoop = *unblockedTrial->findPortalLoop(layer->getId(), rejected);
  auto partial = bw::core::BuildPortalLiquidAdjacency(
      data->getArrangement(), data->getWalls(), cells, {blockedLoop});
  require(partial.diagnostics.empty() && partial.adjacency.size() == 1,
          "blocked hops still required unused incident cells");

  // Independent cycles arbitrate by their smallest stable Portal ID.
  auto named = after->getPortalLoops();
  auto resolveNamed = [&] {
    return bw::core::BuildPortalLiquidAdjacency(after->getArrangement(),
        after->getWalls(), after->getHydraulicCells(), named);
  };
  auto namedResult = resolveNamed();
  std::ranges::reverse(named);
  auto reversedNamed = resolveNamed();
  require(namedResult.diagnostics.size() == 1 && reversedNamed.diagnostics.size() == 1 &&
          namedResult.diagnostics.front().cyclePortalId == rejected &&
          reversedNamed.diagnostics.front().cyclePortalId == rejected &&
          namedResult.adjacency.size() == 4 && reversedNamed.adjacency.size() == 4,
          "independent-cycle conflict handling lost atomicity or stable identity");
  for (size_t i = 0; i < namedResult.adjacency.size(); ++i) {
    auto const& expected = namedResult.adjacency[i];
    auto const& actual = reversedNamed.adjacency[i];
    require(expected.sourceEndpointId == actual.sourceEndpointId &&
            expected.destinationEndpointId == actual.destinationEndpointId &&
            expected.cell0 == actual.cell0 && expected.cell1 == actual.cell1,
            "independent-cycle Liquid arbitration depended on storage order");
  }
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    waterBlockingIsDirectionalAndRegenerates();
    portalAdjacencyIsSeparateAndWaitsForItsSill();
    differingFloorsMapRelativeElevationAndConserveVolume();
    widthDoesNotChangeInstantaneousEquilibriumAndDrainsStillWork();
    consistentAndContradictoryCyclesAreSettledDeterministically();
    directedSpillCannotSkipAnIntermediateSill();
    directedLoopsReachEquilibriumAndOrdinaryDrains();
    directedSpillIntegratesAffineCapacity();
    floodedIntermediateCellsRemainDirectedConduits();
    lateLoopConflictsAreAtomicAndLiquidOnly();
    std::cout << "Portal apertures equilibrate Liquid deterministically\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
