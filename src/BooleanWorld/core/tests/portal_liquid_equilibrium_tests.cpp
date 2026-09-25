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
  if (std::abs(actual - expected) >= Epsilon) {
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
  uint32_t pairId{};
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
  auto pairId = layer->addPortalPair(
      aperture(-50.0f, 0.0f, 5.0f, width),
      aperture(50.0f, 0.0f, 15.0f, width));
  return {world.getWorldData(), pairId};
}

void portalAdjacencyIsSeparateAndWaitsForItsSill() {
  auto below = twoRooms(4.0f);
  require(below.data->getPortalLiquidAdjacency().size() == 1,
          "an active resolved aperture did not expose portal liquid-adjacency");
  auto const& link = below.data->getPortalLiquidAdjacency().front();
  require(link.pairId == below.pairId &&
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
              "Liquid did not pass backward through the Portal pair");
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
  uint32_t closingPair{};
};

CycleResult portalCycle(bool contradictory) {
  World world(400.0f, 10.0f);
  addRoom(world, {-100.0f, 0.0f}, 0.0f, 40.0f, 30.0f);
  addRoom(world, {0.0f, 0.0f}, 10.0f, 50.0f);
  addRoom(world, {100.0f, 0.0f}, 20.0f, 60.0f);
  auto* layer = world.getActiveLayer();
  [[maybe_unused]] auto firstPair = layer->addPortalPair(
      aperture(-75.0f, 0.0f, 5.0f),
      aperture(-25.0f, 0.0f, 15.0f));
  [[maybe_unused]] auto secondPair = layer->addPortalPair(
      aperture(25.0f, 0.0f, 15.0f),
      aperture(75.0f, 0.0f, 25.0f));
  auto closingPair = layer->addPortalPair(
      aperture(125.0f, 0.0f, 25.0f),
      aperture(-125.0f, 0.0f, contradictory ? 6.0f : 5.0f));
  return {world.getWorldData(), closingPair};
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
  require(diagnostic.pairId == contradictory.closingPair &&
              diagnostic.diagnostic ==
                  PortalLiquidDiagnostic::ContradictoryElevationCycle,
          "the contradictory cycle diagnostic did not identify the conflicting pair");
  require(std::ranges::none_of(
              contradictory.data->getPortalLiquidAdjacency(),
              [&](auto const& adjacency) {
                return adjacency.pairId == contradictory.closingPair;
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
              rebuilt.data->getPortalLiquidDiagnostics().front().pairId ==
                  diagnostic.pairId &&
              rebuilt.data->getPortalLiquidDiagnostics().front().diagnostic ==
                  diagnostic.diagnostic,
          "rebuilding an unchanged Portal cycle changed Liquid or diagnostics");
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    portalAdjacencyIsSeparateAndWaitsForItsSill();
    differingFloorsMapRelativeElevationAndConserveVolume();
    widthDoesNotChangeInstantaneousEquilibriumAndDrainsStillWork();
    consistentAndContradictoryCyclesAreSettledDeterministically();
    std::cout << "Portal apertures equilibrate Liquid deterministically\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
