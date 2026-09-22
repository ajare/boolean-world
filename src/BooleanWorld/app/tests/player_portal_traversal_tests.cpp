#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <common/GameDefines.h>
#include <core/LayerBuildStep.h>
#include <core/Portal.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>
#include <willpower/collide/ColliderCircle.h>

#include "PlayerPortalTraversal.h"
#include "WorldCollisionSim.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

struct Fixture {
  bw::core::World world{200.0f, 10.0f};
  bw::core::ArrangementWorldDataPtr data;
  bw::core::ResolvedPortalPair const* pair{};

  Fixture() {
    auto* room = new bw::core::RectanglePolygon(
        bw::core::Primitive::Operation::Union,
        bw::core::Primitive::FillRule::NonZero, 1.0f);
    room->setSize(100.0f, 100.0f);
    world.addPrimitive(room);
    auto* layer = world.getActiveLayer();
    auto pairId = layer->addPortalPair(
        {{-50.0f, 0.0f}, 20.0f, 0.0f, 24.0f},
        {{50.0f, 0.0f}, 20.0f, 0.0f, 24.0f});
    data = world.getWorldData();
    pair = data->findPortalPair(layer->getId(), pairId);
    require(pair && pair->active, "player Portal fixture did not resolve");
  }
};

bw::app::PlayerPortalMotion crossingMotion() {
  return {
      {-40.0f, 0.0f}, 0.0f, 270.0f, -17.0f, {-100.0f, 0.0f}, -23.0f, {-30.0f, 0.0f}};
}

void highSpeedCrossingTransformsCompleteMotionState() {
  Fixture fixture;
  auto motion = crossingMotion();
  auto pitch = motion.pitch;
  auto verticalVelocity = motion.verticalVelocity;
  auto speed = motion.horizontalVelocity.length();
  bw::app::PlayerPortalUpdateState state;
  auto result = bw::app::tryPlayerPortalCrossing(
      *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS, BW_PLAYER_HEIGHT,
      motion, state);
  require(result == bw::app::PlayerPortalCrossingResult::Traversed,
          "high-speed swept Portal crossing was missed");
  auto const& destination = fixture.pair->endpoints[1].aperture;
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

void frameAndVerticalMissesRemainBlocked() {
  Fixture fixture;
  auto beside = crossingMotion();
  beside.position.y = 8.0f;
  bw::app::PlayerPortalUpdateState state;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, beside, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "crossing whose collider overlaps the Portal frame was accepted");

  auto above = crossingMotion();
  above.feetElevation = 8.0f;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, above, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "crossing outside the aperture's vertical bounds was accepted");
}

void collisionSweepContinuesItsTransformedRemainder() {
  Fixture fixture;
  WorldCollisionSim simulation;
  auto collider = std::make_unique<wp::collide::ColliderCircle>(
      wp::Vector2{-40.0f, 0.0f}, BW_PLAYER_RADIUS);
  auto* player = collider.get();
  simulation.addSlidingCollider(std::move(collider));

  struct Endpoint {
    bw::core::ResolvedPortalPair const* pair;
    uint32_t endpoint;
  };
  std::vector<Endpoint> endpoints;
  for (uint32_t endpoint = 0; endpoint < 2; ++endpoint) {
    auto const& aperture = fixture.pair->endpoints[endpoint].aperture;
    auto half = aperture.tangent * (aperture.width * 0.5f);
    endpoints.push_back({fixture.pair, endpoint});
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
            *fixture.data, *endpoints[lineIndex].pair,
            endpoints[lineIndex].endpoint, BW_PLAYER_RADIUS,
            BW_PLAYER_HEIGHT, motion, state);
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
  simulation.update(0.3f);

  auto const& destination = fixture.pair->endpoints[1].aperture;
  require(state.crossings == 1 &&
              (player->getCentre() - destination.centre)
                      .dot(destination.front) > 19.9f,
          "collision sweep did not consume transformed movement after the Portal crossing");
}

void exitSideAndSameUpdateGuardsAreGeometricAndFinite() {
  Fixture fixture;
  auto motion = crossingMotion();
  bw::app::PlayerPortalUpdateState state;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Traversed,
      "initial crossing failed");

  motion.unconsumedMovement =
      -fixture.pair->endpoints[1].aperture.front * 2.0f;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 1, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Blocked,
      "destination endpoint immediately bounced the player back");

  auto const& destination = fixture.pair->endpoints[1].aperture;
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
          *fixture.data, *fixture.pair, 1, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, motion, state) ==
          bw::app::PlayerPortalCrossingResult::Traversed,
      "deliberate reverse traversal stayed suppressed after clearing the plane");

  auto budgetMotion = crossingMotion();
  bw::app::PlayerPortalUpdateState budget;
  budget.crossings = bw::app::MaxPortalCrossingsPerUpdate;
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, budgetMotion, budget) ==
              bw::app::PlayerPortalCrossingResult::Blocked &&
          budget.terminatedByBudgetOrRepeat,
      "named same-update Portal crossing budget did not terminate traversal");

  auto repeatedMotion = crossingMotion();
  bw::app::PlayerPortalUpdateState repeated;
  repeated.visited.push_back({
      {fixture.pair->layerId, fixture.pair->pairId, 0},
      fixture.pair->endpoints[0].aperture.centre,
      {-20.0f, 0.0f}});
  require(
      bw::app::tryPlayerPortalCrossing(
          *fixture.data, *fixture.pair, 0, BW_PLAYER_RADIUS,
          BW_PLAYER_HEIGHT, repeatedMotion, repeated) ==
              bw::app::PlayerPortalCrossingResult::Blocked &&
          repeated.terminatedByBudgetOrRepeat,
      "repeated same-update Portal state did not terminate traversal");
}
}  // namespace

int main() {
  try {
    bw::core::LayerBuildStep::registerCoreTypes();
    highSpeedCrossingTransformsCompleteMotionState();
    frameAndVerticalMissesRemainBlocked();
    collisionSweepContinuesItsTransformedRemainder();
    exitSideAndSameUpdateGuardsAreGeometricAndFinite();
    std::cout << "Player Portal traversal passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
