#include "../../core/tests/PortalTestSupport.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <common/GameDefines.h>
#include <core/LayerBuildStep.h>
#include <core/MeshPrimitive.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Portal.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <willpower/collide/ColliderCircle.h>

#include "PlayerPortalTraversal.h"
#include "PlayerTorchPlacement.h"
#include "WorldCollisionSim.h"
#include "PlayerZone.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

struct Fixture {
  bw::core::World world{200.0f, 10.0f};
  bw::core::ArrangementWorldDataPtr data;
  bw::core::ResolvedPortalLoop const* portalLoop{};

  Fixture(bw::core::AuthoredAperture second =
              {{50.0f, 0.0f}, 20.0f, 0.0f, 24.0f}) {
    auto* room = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero, 1.0f);
    room->setSize(100.0f, 100.0f);
    world.addPrimitive(room);
    auto* layer = world.getActiveLayer();
    auto firstPortalId = bw::test::addPortalCycle(layer,
        {{-50.0f, 0.0f}, 20.0f, 0.0f, 24.0f}, second);
    data = world.getWorldData();
    portalLoop = data->findPortalLoop(layer->getId(), firstPortalId);
    require(portalLoop && portalLoop->active, "player Portal fixture did not resolve");
  }
};

struct MirrorFixture {
  bw::core::World world{200.0f, 10.0f};
  bw::core::ArrangementWorldDataPtr data;
  bw::core::ResolvedPortalLoop const* portalLoop{};
  uint32_t portalId{};

  MirrorFixture() {
    auto* room = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero, 1.0f);
    room->setSize(100.0f, 100.0f);
    world.addPrimitive(room);
    auto* layer = world.getActiveLayer();
    portalId = layer->addPortal({{-50.0f, 7.0f}, 20.0f, 0.0f, 24.0f});
    data = world.getWorldData();
    portalLoop = data->findPortalLoop(
        layer->getId(), portalId);
    require(portalLoop && portalLoop->active &&
                portalLoop->endpoints.size() == 1,
            "player Mirror Portal fixture did not resolve");
  }
};

bw::app::PlayerPortalMotion crossingMotion() {
  return {
      {-40.0f, 0.0f}, 0.0f, 270.0f, -17.0f, {-100.0f, 0.0f}, -23.0f, {-30.0f, 0.0f}};
}

void torchReflectsThroughMirror() {
  MirrorFixture fixture;
  auto place = [&](wp::Vector2 start, wp::Vector2 direction, float reach,
                   float elevation = 12.0f) {
    return bw::app::placePlayerTorch(*fixture.data, start, elevation, direction, reach);
  };
  // Ten units to the plane, then twenty reflected units; tangent is not flipped.
  auto torch = place({-40, 3}, {-1, 0.2f}, std::sqrt(1.04f) * 30);
  require(std::abs(torch.position.x + 30) < 0.01f &&
              std::abs(torch.position.y - 9) < 0.01f && torch.elevation == 12,
          "Mirror Torch rotated its tangent or lost remaining reach");
  for (auto torch : {place({-40, 18}, {-1, 0}, 30),
                     place({-40, 7}, {-1, 0}, 30, 24)}) {
    require(std::abs(torch.position.x - (-50 + BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
            "Mirror Torch leaked through its frame");
  }
  require(std::abs(place({-70, 7}, {1, 0}, 30).position.x -
                   (-50 - BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "Mirror Torch crossed its back side");
  require(std::abs(place({-40, 7}, {-1, 0}, 200).position.x -
                   (50 - BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "reflected Torch ignored the next wall");
  fixture.world.getActiveLayer()->addPortal({{50, 7}, 20, 0, 24});
  fixture.data = fixture.world.getWorldData();
  auto capped = place({-40, 7}, {-1, 0}, 1.0e8f);
  require(std::isfinite(capped.position.x) && std::abs(capped.position.x) < 50 &&
              std::abs(capped.position.y - 7) < 0.01f,
          "facing Mirrors bypassed the Torch traversal budget");
}

void extendedTorchTraversesPortal() {
  Fixture fixture;
  auto torch = bw::app::placePlayerTorch(
      *fixture.data, {-40.0f, 0.0f}, 12.0f, {-1.0f, 0.0f}, 30.0f);
  require(std::abs(torch.position.x - 30.0f) < 0.01f &&
              std::abs(torch.position.y) < 0.01f && torch.elevation == 12.0f,
          "extended Torch did not emerge from the opposite Portal with its remaining reach");
}

void torchReachRespectsPortalFramesAndWalls() {
  Fixture fixture;
  auto place = [&](wp::Vector2 position, wp::Vector2 direction, float distance) {
    return bw::app::placePlayerTorch(*fixture.data, position, 12.0f, direction, distance);
  };
  for (float reach : {0.0f, 5.0f, 9.99f, 10.0f, 10.01f, 30.0f, 5.0f}) {
    auto expected = reach < 10.0f ? -40.0f - reach : 60.0f - reach;
    require(std::abs(place({-40, 0}, {-1, 0}, reach).position.x - expected) < 0.01f,
            "extending/retracting the Torch did not cross at the aperture plane");
  }
  auto lip = bw::app::placePlayerTorch(*fixture.data, {-40, 0}, 24, {-1, 0}, 30);
  require(std::abs(lip.position.x - (-50 + BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "Torch teleported through the aperture's upper frame");
  require(std::abs(place({40, 0}, {1, 0}, 30).position.x + 30) < 0.01f,
          "Torch cannot traverse the reverse endpoint");
  require(std::abs(place({-40, 0}, {-1, 0}, 230).position.x - 30) < 0.01f,
          "Torch lost remaining distance over repeated Portal crossings");
  require(std::abs(place({-40, 15}, {-1, 0}, 30).position.x -
                   (-50 + BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "Torch passed through the solid aperture frame");
  require(std::abs(place({-70, 0}, {1, 0}, 150).position.x -
                   (-50 - BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "Torch crossed a back face/nearer wall to reach a farther Portal");
  auto capped = place({-40, 0}, {-1, 0}, 1.0e8f);
  require(std::isfinite(capped.position.x) && std::abs(capped.position.x) < 50,
          "cyclic Torch path did not stop safely at its traversal budget");

  Fixture turned({{0.0f, 50.0f}, 20.0f, 4.0f, 28.0f});
  auto torch = bw::app::placePlayerTorch(
      *turned.data, {-40, 0}, 12, {-1, 0}, 30);
  require(std::abs(torch.position.x) < 0.01f &&
              std::abs(torch.position.y - 30) < 0.01f && torch.elevation == 16,
          "Torch did not transform its direction and elevation at the exit");
  torch = bw::app::placePlayerTorch(*turned.data, {-40, 0}, 12, {-1, 0}, 150);
  require(std::abs(torch.position.y - (-50 + BW_PLAYER_TORCH_WALL_CLEARANCE)) < 0.01f,
          "Torch did not stop at the first wall beyond the exit");
}

void highSpeedCrossingTransformsCompleteMotionState() {
  Fixture fixture;
  auto motion = crossingMotion();
  auto pitch = motion.pitch;
  auto verticalVelocity = motion.verticalVelocity;
  auto speed = motion.horizontalVelocity.length();
  bw::app::PlayerPortalUpdateState state;
  auto result = bw::app::tryPlayerPortalCrossing(
      *fixture.data, *fixture.portalLoop, fixture.portalLoop->endpoints[0].endpointId,
      BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, motion, state);
  require(result == bw::app::PlayerPortalCrossingResult::Traversed,
          "high-speed swept Portal crossing was missed");
  auto const& destination = fixture.portalLoop->endpoints[1].aperture;
  require((motion.position - destination.centre).dot(destination.front) > 0.0f,
          "player did not emerge in front of the destination plane");
  require(std::abs(
              (motion.position - destination.centre).dot(destination.front) -
              bw::app::PortalExitPlaneEpsilon) < 0.001f,
          "destination plane epsilon was not deterministic");
  require(motion.pitch == pitch && motion.verticalVelocity == verticalVelocity &&
              std::abs(motion.horizontalVelocity.length() - speed) < 0.001f &&
              motion.unconsumedMovement.length() > 0.0f && state.cameraCut,
          "Portal crossing did not preserve pitch/world-up velocity or transform remaining horizontal motion");
  auto transformedForward = wp::Vector2::fromAngle(motion.yaw, wp::Clockwise);
  require(transformedForward.dot(motion.horizontalVelocity.normalisedCopy()) >
              0.999f,
          "Portal facing and horizontal velocity transforms disagreed");
}

void mirrorTraversalReflectsAsymmetricSweptMotion() {
  MirrorFixture fixture;
  auto const initialYaw = wp::Vector2{-4.0f, 2.0f}.clockwiseAngle();
  bw::app::PlayerPortalMotion motion{
      {-40.0f, 9.0f}, 1.5f, initialYaw, -17.0f,
      {-8.0f, 3.0f}, -23.0f, {-30.0f, 6.0f}};
  bw::app::PlayerPortalUpdateState state;
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop, fixture.portalId,
              BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, motion, state) ==
              bw::app::PlayerPortalCrossingResult::Traversed,
          "resolved Mirror Portal did not accept a front-side crossing");

  auto const& aperture = fixture.portalLoop->endpoints.front().aperture;
  require(std::abs(motion.position.x - (-50.0f + bw::app::PortalExitPlaneEpsilon)) <
                  0.001f &&
              std::abs(motion.position.y - 11.0f) < 0.001f &&
              motion.feetElevation == 1.5f,
          "Mirror crossing did not preserve its tangent or feet elevation");
  require(motion.unconsumedMovement.distanceTo({20.0f, 4.0f}) < 0.001f &&
              motion.horizontalVelocity.distanceTo({8.0f, 3.0f}) < 0.001f &&
              wp::Vector2::fromAngle(motion.yaw, wp::Clockwise)
                      .distanceTo(wp::Vector2{4.0f, 2.0f}.normalisedCopy()) <
                  0.001f &&
              motion.pitch == -17.0f && motion.verticalVelocity == -23.0f,
          "Mirror crossing used a rotation instead of canonical reflection");
  auto const identity = bw::app::PortalEndpointIdentity{
      fixture.portalLoop->layerId,
      fixture.portalId};
  require(state.exitSide.active && state.exitSide.endpoint == identity &&
              state.cameraCut,
          "Mirror traversal did not retain stable identity or camera-cut state");

  bw::app::PlayerPortalMotion repeatedMotion{
      {-40.0f, 9.0f}, 1.5f, initialYaw, -17.0f,
      {-8.0f, 3.0f}, -23.0f, {-30.0f, 6.0f}};
  bw::app::PlayerPortalUpdateState repeatedState;
  repeatedState.visited.push_back(
      {identity, {-50.0f, 11.0f}, {-20.0f, 4.0f}});
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop, fixture.portalId,
              BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, repeatedMotion,
              repeatedState) == bw::app::PlayerPortalCrossingResult::Blocked &&
              repeatedState.terminatedByBudgetOrRepeat,
          "Mirror traversal repeat tracking did not use stable Portal identity");

  auto backSide = motion;
  backSide.position = aperture.centre - aperture.front * 10.0f;
  backSide.unconsumedMovement = aperture.front * 20.0f;
  bw::app::PlayerPortalUpdateState backState;
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop, fixture.portalId,
              BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, backSide, backState) !=
              bw::app::PlayerPortalCrossingResult::Traversed,
          "Mirror Portal accepted a back-side crossing");

  WorldCollisionSim simulation;
  auto collider = std::make_unique<wp::collide::ColliderCircle>(
      wp::Vector2{-40.0f, 9.0f}, BW_PLAYER_RADIUS);
  auto* player = collider.get();
  simulation.addSlidingCollider(std::move(collider));
  for (uint32_t wallIndex = 0; wallIndex < fixture.data->getWalls().size(); ++wallIndex) {
    for (auto const& segment : fixture.data->getWallCollisionSegments(wallIndex)) {
      simulation.addLine(segment.v0, segment.v1, wallIndex);
    }
  }
  auto half = aperture.tangent * (aperture.width * 0.5f);
  simulation.addPortalLine(aperture.centre - half, aperture.centre + half);

  bw::app::PlayerPortalMotion swept = motion;
  swept.position = {-40.0f, 9.0f};
  swept.yaw = initialYaw;
  swept.horizontalVelocity = {-100.0f, 20.0f};
  bw::app::PlayerPortalUpdateState sweptState;
  simulation.setPortalHitCallback(
      [&](wp::collide::SweepResult* sweep, uint32_t) {
        swept.position = sweep->oldPosition;
        swept.unconsumedMovement = sweep->movementDesired;
        auto result = bw::app::tryPlayerPortalCrossing(
            *fixture.data, *fixture.portalLoop, fixture.portalId,
            BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, swept, sweptState);
        if (result == bw::app::PlayerPortalCrossingResult::Approaching) {
          return WorldCollisionSim::PortalLineResponse::Ignore;
        }
        if (result != bw::app::PlayerPortalCrossingResult::Traversed) {
          return WorldCollisionSim::PortalLineResponse::Block;
        }
        auto desired = sweep->movementDesired.length();
        auto remaining = swept.unconsumedMovement.length();
        sweep->newPosition = swept.position;
        sweep->movementDone = sweep->newPosition - sweep->oldPosition;
        sweep->movementLeft = swept.unconsumedMovement;
        sweep->distanceMoved = desired - remaining;
        sweep->timeTaken = sweep->distanceMoved / desired;
        return WorldCollisionSim::PortalLineResponse::Traverse;
      });
  player->setMovement({-100.0f, 20.0f});
  simulation.update(0.3f);
  require(sweptState.crossings == 1 &&
              player->getCentre().distanceTo({-29.99f, 15.0f}) < 0.02f &&
              swept.unconsumedMovement.x > 0.0f,
          "high-speed Mirror sweep did not continue its reflected remainder");

  auto laterExit = sweptState.exitSide;
  bw::app::updatePortalExitSideState(
      *fixture.data, player->getCentre(), BW_PLAYER_RADIUS, laterExit);
  require(!laterExit.active,
          "Mirror exit suppression remained after the collider cleared its front side");

  bw::app::PlayerPortalMotion laterMotion{
      {-40.0f, 9.0f}, 1.5f, initialYaw, -17.0f,
      {-8.0f, 3.0f}, -23.0f, {-30.0f, 6.0f}};
  bw::app::PlayerPortalUpdateState laterState;
  laterState.exitSide = laterExit;
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop, fixture.portalId,
              BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT, laterMotion, laterState) ==
              bw::app::PlayerPortalCrossingResult::Traversed,
          "a later-frame Mirror traversal remained suppressed");
}

void frameAndVerticalMissesRemainBlocked() {
  Fixture fixture;
  auto beside = crossingMotion();
  beside.position.y = 8.0f;
  bw::app::PlayerPortalUpdateState state;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, beside, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "crossing whose collider overlaps the Portal frame was accepted");

  auto above = crossingMotion();
  above.feetElevation = 8.0f;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, above, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "crossing outside the aperture's vertical bounds was accepted");
}

void validApproachesDoNotTeleportBeforeTheCentreReachesThePlane() {
  Fixture fixture;
  auto motion = crossingMotion();
  motion.unconsumedMovement = {-1.0f, 0.0f};
  auto initialPosition = motion.position;
  bw::app::PlayerPortalUpdateState state;
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop,
              fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
              BW_PLAYER_HEIGHT, motion, state) ==
              bw::app::PlayerPortalCrossingResult::Approaching &&
              motion.position == initialPosition && state.crossings == 0 &&
              state.visited.empty() && !state.exitSide.active,
          "a valid approach must allow collider overlap without teleporting");
  motion.feetElevation = 30.0f;
  require(bw::app::tryPlayerPortalCrossing(
              *fixture.data, *fixture.portalLoop,
              fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
              BW_PLAYER_HEIGHT, motion, state) ==
              bw::app::PlayerPortalCrossingResult::Blocked,
          "an approach outside the vertical aperture must still block");
}

void collisionSweepContinuesItsTransformedRemainder(bool smallSteps = false) {
  Fixture fixture;
  WorldCollisionSim simulation;
  auto collider = std::make_unique<wp::collide::ColliderCircle>(
      wp::Vector2{-40.0f, 0.0f}, BW_PLAYER_RADIUS);
  auto* player = collider.get();
  simulation.addSlidingCollider(std::move(collider));

  auto routedLoop = *fixture.portalLoop;
  routedLoop.endpoints[0].endpointId = 41;
  routedLoop.endpoints[1].endpointId = 9;
  routedLoop.traversalOrder = {41, 9};
  struct Endpoint {
    bw::core::ResolvedPortalLoop const* portalLoop;
    uint32_t endpointId;
  };
  std::vector<Endpoint> endpoints;
  for (auto const& endpoint : routedLoop.endpoints) {
    auto const& aperture = endpoint.aperture;
    auto half = aperture.tangent * (aperture.width * 0.5f);
    endpoints.push_back({&routedLoop, endpoint.endpointId});
    simulation.addPortalLine(
        aperture.centre - half, aperture.centre + half);
  }

  auto motion = crossingMotion();
  bw::app::PlayerPortalUpdateState state;
  simulation.setPortalHitCallback(
      [&](wp::collide::SweepResult* sweep, uint32_t lineIndex) {
        motion.position = sweep->oldPosition;
        motion.unconsumedMovement = sweep->movementDesired;
        auto result = bw::app::tryPlayerPortalCrossing(
            *fixture.data, *endpoints[lineIndex].portalLoop,
            endpoints[lineIndex].endpointId, BW_PLAYER_RADIUS,
            BW_PLAYER_HEIGHT, motion, state);
        if (result == bw::app::PlayerPortalCrossingResult::Approaching) {
          return WorldCollisionSim::PortalLineResponse::Ignore;
        }
        if (result == bw::app::PlayerPortalCrossingResult::NotCrossing) {
          return state.exitSide.active
                     ? WorldCollisionSim::PortalLineResponse::Ignore
                     : WorldCollisionSim::PortalLineResponse::Block;
        }
        if (result == bw::app::PlayerPortalCrossingResult::Blocked) {
          return WorldCollisionSim::PortalLineResponse::Block;
        }
        auto desired = sweep->movementDesired.length();
        auto remaining = motion.unconsumedMovement.length();
        sweep->newPosition = motion.position;
        sweep->movementDone = sweep->newPosition - sweep->oldPosition;
        sweep->movementLeft = motion.unconsumedMovement;
        sweep->distanceMoved = desired - remaining;
        sweep->timeTaken = sweep->distanceMoved / desired;
        return WorldCollisionSim::PortalLineResponse::Traverse;
      });
  player->setMovement({-100.0f, 0.0f});
  if (smallSteps) {
    for (int frame = 0; frame < 30 && state.crossings == 0; ++frame) {
      simulation.update(0.01f);
    }
  } else {
    simulation.update(0.3f);
  }

  auto const& destination = routedLoop.endpoints[1].aperture;
  require(state.crossings == 1 &&
              state.exitSide.endpoint == bw::app::PortalEndpointIdentity{
                  routedLoop.layerId,
                  routedLoop.endpoints[1].endpointId} &&
              (player->getCentre() - destination.centre)
                      .dot(destination.front) > (smallSteps ? 0.0f : 19.9f),
          "collision sweep did not consume transformed movement after the Portal crossing");

  auto const& trace = simulation.getPlayerMovementTrace();
  for (auto initial : {bw::core::ZoneId::Euclidean, bw::core::ZoneId::NegativeSpace}) {
    bw::app::PlayerZone zone;
    zone.set(initial);
    zone.applyResolvedMovement(*fixture.data, trace);
    require(zone.current() == initial, "resolved Portal traversal changed player Zone");
  }
  auto relocation = std::find_if(
      trace.begin(), trace.end(), [](auto const& segment) {
        return segment.type ==
               WorldCollisionSim::PlayerMovementSegmentType::PortalRelocation;
      });
  require(relocation != trace.end(),
          "Portal jump was not explicit in the resolved movement trace");
  require(relocation != trace.begin() &&
              std::prev(relocation)->type ==
                  WorldCollisionSim::PlayerMovementSegmentType::Swept &&
              std::prev(relocation)->to.distanceTo(relocation->from) < 0.001f,
          "Portal trace did not preserve ordinary movement before relocation");
  if (!smallSteps) {
    // Overlay generated, passable Borders on the ordinary portions of this
    // real Portal sweep. The source exit assigns Negative Space, the jump
    // preserves it, and the destination entry repairs it to Euclidean.
    using namespace bw::core;
    auto box = [](wp::Vector2 centre) {
      auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
          Primitive::Operation::Union,
          {{{{{centre.x - 1, centre.y - 1}}, {{centre.x + 1, centre.y - 1}},
              {{centre.x + 1, centre.y + 1}}, {{centre.x - 1, centre.y + 1}}}, {}}}));
      auto proxy = mesh->createEditingProxy();
      for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
           edge = proxy->getNextEdgeIndex(edge))
        proxy->setEdgeCollisionOverride(edge, false);
      proxy->commitTo(*mesh);
      return mesh;
    };
    auto source = box(trace.front().from);
    auto destinationBox = box(trace.back().to);
    ArrangementWorldData borders(arr::BuildArrangement(
        SnapshotPrimitives({source.get(), destinationBox.get()})),
        wp::BoundingBox({-500, -500}, {500, 500}), 4);
    bw::app::PlayerZone zone;
    for (auto it = trace.begin(); it != relocation; ++it)
      zone.applyResolvedMovement(borders, {*it});
    require(zone.current() == ZoneId::NegativeSpace, "pre-Portal Border was skipped");
    zone.applyResolvedMovement(borders, {*relocation});
    require(zone.current() == ZoneId::NegativeSpace, "Portal jump assigned a Zone");
    for (auto it = std::next(relocation); it != trace.end(); ++it)
      zone.applyResolvedMovement(borders, {*it});
    require(zone.current() == ZoneId::Euclidean, "post-Portal Border was skipped");
  }
  if (smallSteps) {
    require(relocation->to.distanceTo(player->getCentre()) < 0.001f,
            "small-step Portal trace did not end at its relocation");
  } else {
    require(std::next(relocation) != trace.end() &&
                std::next(relocation)->type ==
                    WorldCollisionSim::PlayerMovementSegmentType::Swept &&
                relocation->to.distanceTo(std::next(relocation)->from) <
                    0.001f,
            "Portal trace did not retain ordinary movement after relocation");
  }
}

void threeEndpointLoopTraversesOnlyInDirectedOrder(bool named = false) {
  bw::core::World world{200.0f, 10.0f};
  auto* room = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  room->setSize(100.0f, 100.0f);
  world.addPrimitive(room);
  auto* layer = world.getActiveLayer();
  uint32_t firstPortalId = 0;
  uint32_t thirdId;
  if (named) {
    auto a = layer->addPortal({{-50, 7}, 28, 0, 24});
    auto b = layer->addPortal({{50, -11}, 20, 0, 24});
    thirdId = layer->addPortal({{13, 50}, 24, 0, 24});
    layer->setPortalTarget(a, b);
    layer->setPortalTarget(b, thirdId);
    layer->setPortalTarget(thirdId, a);
  } else {
    firstPortalId = bw::test::addPortalCycle(layer,
        {{-50.0f, 0.0f}, 20.0f, 0.0f, 24.0f},
        {{50.0f, 0.0f}, 20.0f, 0.0f, 24.0f});
    thirdId = bw::test::insertPortalAfter(layer, layer->getPortal(firstPortalId)->getTargetId(), {{0.0f, 50.0f}, 20.0f, 0.0f, 24.0f});
  }
  auto data = world.getWorldData();
  auto const* loop = data->findPortalLoop(layer->getId(), 0);
  require(loop && loop->active && loop->endpoints.size() == 3,
          "three-endpoint player Portal fixture did not resolve");

  for (auto sourceId : loop->traversalOrder) {
    auto const* source = bw::core::FindPortalEndpoint(*loop, sourceId);
    auto const* destination = bw::core::NextPortalEndpoint(*loop, sourceId);
    auto movement = -source->aperture.front * 30.0f;
    bw::app::PlayerPortalMotion motion{
        source->aperture.centre + source->aperture.front * 10.0f,
        0.0f, movement.clockwiseAngle(), 0.0f,
        movement.normalisedCopy() * 100.0f, 0.0f, movement};
    bw::app::PlayerPortalUpdateState state;
    require(
        bw::app::tryPlayerPortalCrossing(
            *data, *loop, sourceId, BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT,
            motion, state) ==
            bw::app::PlayerPortalCrossingResult::Traversed &&
            (motion.position - destination->aperture.centre)
                    .dot(destination->aperture.front) > 0.0f,
        "a directed three-endpoint Portal hop used the wrong destination");
  }

  auto const* first = bw::core::FindPortalEndpoint(*loop, 0);
  auto reverseMovement = first->aperture.front * 30.0f;
  bw::app::PlayerPortalMotion reverse{
      first->aperture.centre - first->aperture.front * 10.0f,
      0.0f, reverseMovement.clockwiseAngle(), 0.0f,
      reverseMovement.normalisedCopy() * 100.0f, 0.0f, reverseMovement};
  bw::app::PlayerPortalUpdateState state;
  require(
      bw::app::tryPlayerPortalCrossing(
          *data, *loop, 0, BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT,
          reverse, state) != bw::app::PlayerPortalCrossingResult::Traversed,
      "a Portal endpoint accepted direct reverse traversal");
  require(thirdId == 2, "three-endpoint fixture did not use stable identity");
}

void exitSideAndSameUpdateGuardsAreGeometricAndFinite() {
  Fixture fixture;
  auto motion = crossingMotion();
  bw::app::PlayerPortalUpdateState state;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Traversed,
      "initial crossing failed");

  motion.unconsumedMovement =
      -fixture.portalLoop->endpoints[1].aperture.front * 2.0f;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[1].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "destination endpoint immediately bounced the player back");

  auto const& destination = fixture.portalLoop->endpoints[1].aperture;
  motion.position = destination.centre +
                    destination.front *
                        (BW_PLAYER_RADIUS +
                         bw::app::PortalExitPlaneEpsilon + 0.1f);
  bw::app::updatePortalExitSideState(
      *fixture.data, motion.position, BW_PLAYER_RADIUS, state.exitSide);
  require(!state.exitSide.active,
          "exit-side state did not clear after the collider cleared the plane");
  motion.unconsumedMovement =
      -destination.front * (BW_PLAYER_RADIUS + 1.0f);
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[1].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Traversed,
      "deliberate reverse traversal stayed suppressed after clearing the plane");

  auto budgetMotion = crossingMotion();
  bw::app::PlayerPortalUpdateState budget;
  budget.crossings = bw::app::MaxPortalCrossingsPerUpdate;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, budgetMotion, budget) ==
              bw::app::PlayerPortalCrossingResult::Blocked &&
          budget.terminatedByBudgetOrRepeat,
      "named same-update Portal crossing budget did not terminate traversal");

  auto repeatedMotion = crossingMotion();
  bw::app::PlayerPortalUpdateState repeated;
  repeated.visited.push_back({{fixture.portalLoop->layerId,
                               fixture.portalLoop->endpoints[0].endpointId},
                              fixture.portalLoop->endpoints[0].aperture.centre,
                              {-20.0f, 0.0f}});
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.portalLoop,
          fixture.portalLoop->endpoints[0].endpointId, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, repeatedMotion, repeated) ==
              bw::app::PlayerPortalCrossingResult::Blocked &&
          repeated.terminatedByBudgetOrRepeat,
      "repeated same-update Portal state did not terminate traversal");
}
}  // namespace

int main() {
  try {
    torchReflectsThroughMirror();
    extendedTorchTraversesPortal();
    torchReachRespectsPortalFramesAndWalls();
    bw::core::LayerBuildStep::registerCoreTypes();
    highSpeedCrossingTransformsCompleteMotionState();
    mirrorTraversalReflectsAsymmetricSweptMotion();
    frameAndVerticalMissesRemainBlocked();
    validApproachesDoNotTeleportBeforeTheCentreReachesThePlane();
    collisionSweepContinuesItsTransformedRemainder();
    collisionSweepContinuesItsTransformedRemainder(true);
    threeEndpointLoopTraversesOnlyInDirectedOrder();
    threeEndpointLoopTraversesOnlyInDirectedOrder(true);
    exitSideAndSameUpdateGuardsAreGeometricAndFinite();
    std::cout << "Player Portal traversal passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
