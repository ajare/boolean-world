#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <willpower/collide/ColliderCircle.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>
#include <core/WorldTriggerLine.h>
#include "PlayerZone.h"
#include "PlayerWorldReconciliation.h"

namespace {
void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}
void movementAndValidation() {
  using namespace bw::core;
  auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{{{{-64, -64}}, {{64, -64}}, {{64, 64}}, {{-64, 64}}}, {}}}));
  auto properties = mesh->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 128.0f;
  mesh->setProperties(properties);
  auto proxy = mesh->createEditingProxy();
  for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge)) {
    proxy->setEdgeOtherZone(edge, ZoneId::Phantom);
    proxy->setEdgeCollisionOverride(edge, false);
    proxy->setEdgeVisible(edge, false);
  }
  proxy->commitTo(*mesh);
  auto build = [&] {
    return ArrangementWorldData(arr::BuildArrangement(SnapshotPrimitives({mesh.get()})),
        wp::BoundingBox({-256, -256}, {512, 512}), 16.0f);
  };
  auto data = build();
  auto run = [&](wp::Vector2 from, wp::Vector2 movement, float feet, ZoneId initial,
                 bool barrier) {
    WorldCollisionSim sim;
    auto owner = std::make_unique<wp::collide::ColliderCircle>(from, BW_PLAYER_RADIUS);
    auto* player = owner.get();
    sim.addSlidingCollider(std::move(owner));
    for (uint32_t index = 0; index < data.getWalls().size(); ++index) {
      auto frame = arr::OrientArrangementWall(data.getArrangement(), data.getWalls()[index]);
      sim.addZoneLine(frame.v0, frame.v1, index);
    }
    if (barrier) sim.addLine({0, -64}, {0, 64}, 999);
    sim.addPortalLine({90, -64}, {90, 64});
    sim.setPortalHitCallback([](auto*, auto) -> WorldCollisionSim::PortalLineResponse {
      throw std::runtime_error("Phantom interacted with a Portal");
    });
    bw::app::PlayerZone zone;
    zone.set(initial);
    float returnFraction = -1.0f;
    zone.configureMovement(sim, data, feet, [&](auto destination, auto const&) {
      if (destination == ZoneId::Euclidean) returnFraction = sim.remainingFrameFraction();
    });
    player->setMovement(movement);
    sim.updateWithinBoundary(1.0f, {{-256, -256}, {512, 512}});
    if (initial == ZoneId::Phantom && feet == 0 && movement.x < 0)
      require(std::abs(returnFraction - (1.0f - 46.0f / 150.0f)) < 0.0001f,
          "return physics included the Phantom part of the frame");
    auto const& trace = sim.getPlayerMovementTrace();
    zone.rememberMovement(trace);
    require(!trace.empty() && trace.front().physicallyAbsent == (initial == ZoneId::Phantom),
        "swept trigger trace lost the source Zone");
    require(trace.back().physicallyAbsent == (zone.current() == ZoneId::Phantom),
        "swept trigger trace lost the destination Zone");
    World triggerWorld(512.0f, 16.0f);
    auto* trigger = new WorldTriggerLine({80, -64}, {80, 64}, WorldTriggerLineSide::Both);
    triggerWorld.addTriggerLine(trigger);
    for (auto const& segment : trace)
      if (!segment.physicallyAbsent && segment.type == WorldCollisionSim::PlayerMovementSegmentType::Swept)
        triggerWorld.checkPlayerTriggers(segment.from, segment.to, BW_PLAYER_RADIUS,
            SelectLayer(triggerWorld.getActiveLayer()->getId()));
    require(trigger->getTotalTriggerCount() == 0, "Phantom fired a WorldTriggerLine");
    return std::pair{player->getCentre(), zone.current()};
  };
  auto returned = run({110, 0}, {-150, 0}, 0, ZoneId::Phantom, true);
  require(returned.second == ZoneId::Euclidean, "Phantom return did not select Euclidean");
  require(returned.first.x >= BW_PLAYER_RADIUS - 0.01f && returned.first.x < 64,
      "remaining return movement bypassed Euclidean collision");
  for (float feet : {-1.0f, 129.0f}) {
    auto missed = run({110, 0}, {-150, 0}, feet, ZoneId::Phantom, true);
    require(missed.second == ZoneId::Phantom && missed.first.x < 0,
        "out-of-height return interacted with geometry");
  }
  auto exited = run({32, 0}, {78, 0}, 0, ZoneId::Euclidean, false);
  require(exited.second == ZoneId::Phantom && std::abs(exited.first.x - 110.0f) < 0.001f,
      "Phantom entry did not disable Portal interaction in the same frame");
  auto reverse = run({32, 0}, {78, 0}, 0, ZoneId::Phantom, false);
  require(reverse.second == ZoneId::Phantom, "invisible reverse side changed Zone");
  auto extent = run({110, 0}, {1000, 0}, 0, ZoneId::Phantom, false);
  require(extent.first.x <= 256 - BW_PLAYER_RADIUS + 0.01f, "Phantom escaped engine extent");
  {
    WorldCollisionSim sim;
    auto owner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{80, 0}, BW_PLAYER_RADIUS);
    auto* player = owner.get();
    sim.addSlidingCollider(std::move(owner));
    for (uint32_t index = 0; index < data.getWalls().size(); ++index) {
      auto frame = arr::OrientArrangementWall(data.getArrangement(), data.getWalls()[index]);
      sim.addZoneLine(frame.v0, frame.v1, index);
    }
    float feet = 0;
    bw::app::PlayerZone zone;
    zone.set(ZoneId::Phantom);
    auto step = [&](float dx) {
      zone.configureMovement(sim, data, feet);
      player->setMovement({dx, 0});
      zone.resolveMovement(sim, 1.0f);
      zone.rememberMovement(sim.getPlayerMovementTrace());
    };
    step(-16);
    require(zone.current() == ZoneId::Phantom, "endpoint contact changed Phantom Zone");
    step(16);
    require(zone.current() == ZoneId::Phantom, "touch and retreat changed Phantom Zone");
    step(-16);
    step(-16);
    require(zone.current() == ZoneId::Euclidean && std::abs(player->getCentre().x - 48) < 0.001f,
        "frame-split Phantom crossing lost its approach side");
  }
  auto frozen = bw::app::stepPlayerVerticalPhysics({17.0f, -100.0f},
      {.inWorld = true, .floorElevation = 90.0f, .liquidSurface = 200.0f,
       .swimEffort = 1.0f, .frameTime = 10.0f, .zone = ZoneId::Phantom});
  require(frozen.feetElevation == 17.0f && frozen.verticalVelocity == 0.0f,
      "Phantom retained gravity, swimming or floor following");
  auto reconciled = bw::app::reconcilePlayerAfterWorldRebuild(data, data,
      {1000, 1000}, frozen, ZoneId::Phantom);
  require(!reconciled.requiresLocationRecovery && !reconciled.regrounded &&
      reconciled.vertical.feetElevation == frozen.feetElevation,
      "Phantom rebuild attempted physical-world recovery");
  auto edge = proxy->getFirstEdgeIndex();
  proxy->setEdgeVisible(edge, true);
  proxy->commitTo(*mesh);
  bool rejected = false;
  try { (void)build(); } catch (std::invalid_argument const& error) {
    rejected = std::string(error.what()).find("Phantom") != std::string::npos;
  }
  require(rejected, "visible Phantom Border was accepted");
  proxy->setEdgeCollisionOverride(edge, true);
  proxy->commitTo(*mesh);
  (void)build();
}
}
int main() {
  try { movementAndValidation(); }
  catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
