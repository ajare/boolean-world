#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <willpower/collide/ColliderCircle.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
#include <core/World.h>

#include <core/MeshPrimitive.h>
#include "PlayerZone.h"
#include "PlayerLocation.h"
#include "WorldCollisionSim.h"

namespace {
struct CollisionWall {
  wp::Vector2 v0;
  wp::Vector2 v1;
  wp::Vector2 playableNormal;
};

std::shared_ptr<bw::core::ArrangementWorldData> makeRegressionWorldData() {
  using bw::core::Primitive;

  bw::core::World world(8192.0f, 8192.0f);
  auto rectangle = new bw::core::RectanglePolygon(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 1.0f);
  rectangle->setSize(100.0f, 100.0f);
  world.addPrimitive(rectangle);

  auto triangle = new bw::core::RegularPolygon(
      Primitive::Operation::Difference, Primitive::FillRule::NonZero, 3);
  triangle->setPosition({0.0f, -70.0f});
  triangle->setSize(100.0f, 100.0f);
  triangle->setPriority(1);
  world.addPrimitive(triangle);

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(&world);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(), 64.0f);
}

std::vector<CollisionWall> makeRegressionCollisionWalls() {
  auto data = makeRegressionWorldData();
  auto const& arrangement = data->getArrangement();
  std::vector<CollisionWall> result;
  for (auto const& wall : data->getWalls()) {
    if (wall.kind != bw::core::arr::ArrangementWallKind::Border) {
      continue;
    }
    auto const& edge = arrangement.edges[wall.edge];
    auto const& fixed0 = arrangement.vertices[edge.v[0]];
    auto const& fixed1 = arrangement.vertices[edge.v[1]];
    wp::Vector2 v0{
        bw::core::arr::ToWorldCoordinate(fixed0.x),
        bw::core::arr::ToWorldCoordinate(fixed0.y)};
    wp::Vector2 v1{
        bw::core::arr::ToWorldCoordinate(fixed1.x),
        bw::core::arr::ToWorldCoordinate(fixed1.y)};
    auto leftNormal = (v1 - v0).normalisedCopy().perpendicular();
    auto leftIsPlayable = arrangement.faces[edge.face[0]].solid;
    result.push_back({v0, v1, leftIsPlayable ? leftNormal : -leftNormal});
  }
  return result;
}

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, float tolerance, std::string const& message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(
        message + ": expected " + std::to_string(expected) +
        ", got " + std::to_string(actual));
  }
}

void generatedBorderZonesFollowResolvedMovement() {
  using namespace bw::core;
  for (auto other : {ZoneId::Euclidean, ZoneId::NegativeSpace}) {
    for (bool collides : {false, true}) {
      for (bool visible : {false, true}) {
        auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
            Primitive::Operation::Union,
            {{{{{-10, -10}}, {{10, -10}}, {{10, 10}}, {{-10, 10}}}, {}}}));
        auto proxy = mesh->createEditingProxy();
        for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
             edge = proxy->getNextEdgeIndex(edge)) {
          proxy->setEdgeOtherZone(edge, other);
          proxy->setEdgeCollisionOverride(edge, collides);
          proxy->setEdgeVisible(edge, visible);
        }
        proxy->commitTo(*mesh);
        auto build = [&] {
          return ArrangementWorldData(arr::BuildArrangement(SnapshotPrimitives({mesh.get()})),
              wp::BoundingBox({-30, -30}, {30, 30}), 4.0f);
        };
        auto data = build();
        for (auto initial : {ZoneId::Euclidean, ZoneId::NegativeSpace}) {
          for (bool outward : {false, true}) {
            WorldCollisionSim sim;
            for (auto i : data.getWallsNearForTraversal({10, 0}, 30, {outward ? 5.0f : 15.0f, 0}, false))
              for (auto const& segment : data.getWallCollisionSegments(i))
                sim.addLine(segment.v0, segment.v1, i);
            wp::Vector2 start{outward ? 5.0f : 15.0f, 0};
            auto owner = std::make_unique<wp::collide::ColliderCircle>(start, 0.5f);
            auto player = owner.get();
            sim.addSlidingCollider(std::move(owner));
            bw::app::PlayerZone zone;
            require(zone.current() == ZoneId::Euclidean, "new player must start Euclidean");
            zone.set(initial);
            player->setMovement({outward ? 10.0f : -10.0f, 0});
            zone.resolveMovement(sim, 1.0f);
            zone.applyResolvedMovement(data, sim.getPlayerMovementTrace());
            require(zone.current() == (collides ? initial : outward ? other : ZoneId::Euclidean),
                    "generated Border assigned wrong resolved Zone");
            require(collides ? std::abs(player->getCentre().x - 10) >= 0.499f
                             : std::abs(player->getCentre().x - (outward ? 15 : 5)) < 0.001f,
                    "Zone changed two-sided collision behavior");
            auto rebuilt = build();
            auto before = zone.current();
            zone.applyResolvedMovement(rebuilt, {{{0, 0}, {20, 0},
                WorldCollisionSim::PlayerMovementSegmentType::PortalRelocation}});
            require(zone.current() == before, "snapshot/relocation changed Zone");
            zone.initialize();
            require(zone.current() == ZoneId::Euclidean, "respawn/map entry must reset Zone");
          }
        }
        require(bw::app::queryPlayerZoneCrossings(data, {5, 0}, {10, 0}).empty(),
                "touching Border changed Zone");
        require(bw::app::queryPlayerZoneCrossings(data, {10, -5}, {10, 5}).empty(),
                "travelling along Border changed Zone");
        require(bw::app::queryPlayerZoneCrossings(data, {5, 5}, {15, 15}).empty(),
                "endpoint-only contact changed Zone");
      }
    }
  }
}

void orderedZoneCrossingsAndFrameBoundaries() {
  using namespace bw::core;
  using bw::app::PlayerZone;
  auto mesh = std::unique_ptr<MeshPrimitive>(MeshPrimitive::fromTree(
      Primitive::Operation::Union,
      {{{{{-10, -10}}, {{10, -10}}, {{10, 0}}, {{10, 10}}, {{-10, 10}}}, {}}}));
  auto proxy = mesh->createEditingProxy();
  for (auto edge = proxy->getFirstEdgeIndex(); !proxy->edgeIndexIterationFinished(edge);
       edge = proxy->getNextEdgeIndex(edge))
    proxy->setEdgeCollisionOverride(edge, false);
  proxy->commitTo(*mesh);
  ArrangementWorldData data(arr::BuildArrangement(SnapshotPrimitives({mesh.get()})),
      wp::BoundingBox({-50, -50}, {50, 50}), 4.0f);
  auto crossings = bw::app::queryPlayerZoneCrossings(data, {-20, 0}, {20, 0});
  require(crossings.size() == 2 && crossings[0].fraction < crossings[1].fraction &&
              crossings[0].destination == ZoneId::Euclidean &&
              crossings[1].destination == ZoneId::NegativeSpace,
          "connected Borders did not cross once each in travel order");
  for (float dt : {1.0f, 0.25f, 0.125f}) {
    WorldCollisionSim sim;
    auto owner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-20, 0}, 0.5f);
    auto player = owner.get();
    sim.addSlidingCollider(std::move(owner));
    PlayerZone zone;
    zone.set(ZoneId::NegativeSpace);
    for (int frame = 0; frame < int(1 / dt); ++frame) {
      player->setMovement({40, 0});
      zone.resolveMovement(sim, dt);
      zone.applyResolvedMovement(data, sim.getPlayerMovementTrace());
      if (player->getCentre().x > -10 && player->getCentre().x < 10)
        require(zone.current() == ZoneId::Euclidean, "split-frame entry did not repair Zone");
    }
    require(zone.current() == ZoneId::NegativeSpace,
            "high-speed and split-frame crossings disagree");
  }
  PlayerZone zone;
  zone.applyResolvedMovement(data, {{{0, 1}, {10, 1}}});
  require(zone.current() == ZoneId::Euclidean, "endpoint contact assigned Zone");
  zone.applyResolvedMovement(data, {{{10, 1}, {15, 1}}});
  require(zone.current() == ZoneId::NegativeSpace, "split crossing lost its approach side");
  zone.set(ZoneId::NegativeSpace);
  zone.applyResolvedMovement(data, {{{15, 1}, {10, 1}}, {{10, 1}, {15, 1}}});
  require(zone.current() == ZoneId::NegativeSpace, "touch and retreat assigned Zone");
  zone.set(ZoneId::Euclidean);
  zone.applyResolvedMovement(data, {{{0, 0}, {20, 0}},
      {{20, 0}, {-20, 0}, WorldCollisionSim::PlayerMovementSegmentType::PortalRelocation}});
  require(zone.current() == ZoneId::NegativeSpace, "relocation erased pre-Portal crossing");
  zone.applyResolvedMovement(data, {{{-20, 0}, {0, 0}}});
  require(zone.current() == ZoneId::Euclidean, "post-Portal crossing was skipped");

  for (bool rejectedExit : {false, true}) {
    WorldCollisionSim portalSim;
    auto owner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{0, 1}, 0.5f);
    auto player = owner.get();
    portalSim.addSlidingCollider(std::move(owner));
    portalSim.addPortalLine({5, -5}, {5, 5});
    if (rejectedExit) portalSim.addLine({20, -5}, {20, 5}, 0);
    portalSim.setPortalHitCallback([&](wp::collide::SweepResult* sweep, uint32_t) {
      if (!rejectedExit) return WorldCollisionSim::PortalLineResponse::Block;
      sweep->newPosition = {20, 1}; // Overlapping exit is rejected by the simulation.
      sweep->movementDone = sweep->newPosition - sweep->oldPosition;
      sweep->distanceMoved = 5;
      sweep->movementLeft = {0, 0};
      return WorldCollisionSim::PortalLineResponse::Traverse;
    });
    zone.set(ZoneId::Euclidean);
    player->setMovement({15, 0});
    zone.resolveMovement(portalSim, 1);
    zone.applyResolvedMovement(data, portalSim.getPlayerMovementTrace());
    require(zone.current() == ZoneId::Euclidean, "failed Portal created phantom Zone crossing");
    if (rejectedExit)
      require(player->getCentre().distanceTo({0, 1}) < 0.001f &&
                  portalSim.getPlayerMovementTrace().empty(),
              "rejected Portal leaked a movement trace");
  }

  // A real slide skirts the top-left corner without entering. Its chord
  // cuts through the solid and must not substitute for the two sweeps.
  WorldCollisionSim sim;
  sim.addLine({-30, 12}, {30, 12}, 0);
  auto owner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-20, 0}, 0.5f);
  auto player = owner.get();
  sim.addSlidingCollider(std::move(owner));
  zone.set(ZoneId::Euclidean);
  player->setMovement({20, 40});
  zone.resolveMovement(sim, 1);
  auto const& trace = sim.getPlayerMovementTrace();
  require(trace.size() >= 2, "slide fixture did not produce multiple sweeps");
  zone.applyResolvedMovement(data, trace);
  require(zone.current() == ZoneId::Euclidean, "slide used the start/end chord");
  require(!bw::app::queryPlayerZoneCrossings(data, {-20, 0}, player->getCentre()).empty(),
          "slide fixture chord did not cross Borders");
}

void playerLocationUsesResolvedPosition() {
  auto data = makeRegressionWorldData();
  wp::Vector2 const preMovementPosition{4000.0f, 4000.0f};
  wp::Vector2 const resolvedPosition{0.0f, 0.0f};

  require(data->getContainingFaceIndex(preMovementPosition) == ~0u,
          "Regression fixture pre-movement position must be outside the world");

  auto location =
      bw::app::evaluatePlayerLocation(*data, resolvedPosition, 6.0f);
  auto expectedFace = data->getContainingFaceIndex(resolvedPosition);
  require(expectedFace != ~0u,
          "Regression fixture resolved position must be inside the world");
  require(location.faceIndex == static_cast<int32_t>(expectedFace),
          "Player face was not evaluated at the resolved position");
  require(location.intersectingWallIndex ==
              data->circleIntersectsWall(resolvedPosition, 6.0f),
          "Player wall intersection was not evaluated at the resolved position");
}

void generatedWallsSlideFromThePlayableSide() {
  constexpr float radius = 6.0f;
  auto walls = makeRegressionCollisionWalls();
  require(walls.size() == 7, "Unexpected collision fixture topology");

  for (uint32_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& testedWall = walls[wallIndex];
    auto tangent = (testedWall.v1 - testedWall.v0).normalisedCopy();
    auto midpoint = (testedWall.v0 + testedWall.v1) * 0.5f;

    for (float tangentDirection : {-1.0f, 1.0f}) {
      WorldCollisionSim simulation;
      for (uint32_t index = 0; index < walls.size(); ++index) {
        simulation.addLine(walls[index].v0, walls[index].v1, index);
      }

      auto start = midpoint + testedWall.playableNormal * (radius + 0.001f);
      auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(start, radius);
      auto player = playerOwner.get();
      simulation.addSlidingCollider(std::move(playerOwner));
      player->setMovement(
          tangent * (2.0f * tangentDirection) - testedWall.playableNormal);
      simulation.update(1.0f);

      auto movement = player->getCentre() - start;
      requireNear(movement.dot(tangent), 2.0f * tangentDirection, 0.01f,
                  "Player caught on generated wall " +
                      std::to_string(wallIndex));
      requireNear((player->getCentre() - midpoint).dot(testedWall.playableNormal),
                  radius + 0.001f, 0.01f,
                  "Player crossed generated wall " +
                      std::to_string(wallIndex));
    }
  }
}

void callerCulledWorldLinesDoNotCreateASecondSpatialGrid() {
  WorldCollisionSim simulation;
  require(simulation.getStaticLinesGrid() == nullptr,
          "World collision lines must not create an unused spatial grid");
  require(simulation.getCollidersGrid() == nullptr,
          "World collision colliders must not create an unused spatial grid");

  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(
      wp::Vector2{3998.0f, 4000.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({4000.0f, 3990.0f}, {4000.0f, 4010.0f}, 0);

  player->setMovement({3.0f, 0.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, 3999.499f, 0.002f,
              "Caller-culled wall outside the old grid extents was ignored");
}

void directMovementProducesOneResolvedSegment() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(
      wp::Vector2{-2.0f, 1.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));

  player->setMovement({3.0f, 2.0f});
  simulation.update(1.0f);

  auto const& trace = simulation.getPlayerMovementTrace();
  require(trace.size() == 1 &&
              trace.front().type ==
                  WorldCollisionSim::PlayerMovementSegmentType::Swept &&
              trace.front().from.distanceTo({-2.0f, 1.0f}) < 0.0001f &&
              trace.front().to.distanceTo({1.0f, 3.0f}) < 0.0001f,
          "direct movement did not expose one resolved segment");
}

void diagonalMovementSlidesAlongWall() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-2.0f, 0.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, 0);

  player->setMovement({3.0f, 2.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 2.0f, 0.002f,
              "Player did not preserve upward movement along the wall");

  auto const& trace = simulation.getPlayerMovementTrace();
  require(trace.size() == 2 &&
              trace[0].type ==
                  WorldCollisionSim::PlayerMovementSegmentType::Swept &&
              trace[1].type ==
                  WorldCollisionSim::PlayerMovementSegmentType::Swept,
          "Diagonal slide did not expose both ordered swept segments");
  require(trace[0].from.distanceTo({-2.0f, 0.0f}) < 0.002f &&
              trace[0].to.distanceTo(trace[1].from) < 0.0001f &&
              trace[1].to.distanceTo(player->getCentre()) < 0.0001f,
          "Diagonal slide trace is not a continuous resolved path");
  requireNear(trace[0].to.y, 1.0f, 0.002f,
              "Diagonal slide trace collapsed to its start-to-end chord");
}

void perpendicularMovementStopsAtWall() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-2.0f, 1.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, 0);

  player->setMovement({3.0f, 0.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 1.0f, 0.002f,
              "Perpendicular impact introduced tangential movement");
  auto const& trace = simulation.getPlayerMovementTrace();
  require(trace.size() == 1 &&
              trace.front().from.distanceTo({-2.0f, 1.0f}) < 0.0001f &&
              trace.front().to.distanceTo(player->getCentre()) < 0.0001f,
          "Perpendicular collision did not expose its resolved movement");
}

void smallMovementStillSlidesAlongWall() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-0.55f, 0.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, 0);

  player->setMovement({0.1f, 0.05f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 0.05f, 0.002f,
              "Small tangential movement was discarded");
  auto const& trace = simulation.getPlayerMovementTrace();
  require(trace.size() == 2 &&
              trace.front().from.distanceTo({-0.55f, 0.0f}) < 0.0001f &&
              trace.back().to.distanceTo(player->getCentre()) < 0.0001f,
          "Small collision step did not retain its ordered slide trace");
}

void straightWallSlidingIsRotationInvariant() {
  constexpr float radius = 6.0f;
  constexpr int frameCount = 20;

  for (int angleDegrees = 0; angleDegrees < 360; angleDegrees += 5) {
    auto angle = static_cast<float>(angleDegrees) * 3.14159265358979323846f / 180.0f;
    wp::Vector2 tangent{std::cos(angle), std::sin(angle)};
    auto normal = tangent.perpendicular();

    for (float tangentMovement : {-1.0f, 1.0f}) {
      for (float inwardMovement : {0.01f, 0.2f, 1.0f}) {
        WorldCollisionSim simulation;
        auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(normal * (radius + 0.001f), radius);
        auto player = playerOwner.get();
        simulation.addSlidingCollider(std::move(playerOwner));
        simulation.addLine(tangent * -100.0f, tangent * 100.0f, 0);

        for (int frame = 0; frame < frameCount; ++frame) {
          player->setMovement(tangent * tangentMovement - normal * inwardMovement);
          simulation.update(1.0f);
        }

        auto position = player->getCentre();
        auto tangentPosition = position.dot(tangent);
        auto normalPosition = position.dot(normal);
        if (std::abs(tangentPosition - tangentMovement * frameCount) > 0.02f ||
            std::abs(normalPosition - (radius + 0.001f)) > 0.02f) {
          throw std::runtime_error(
              "Straight-wall slide caught at angle " +
              std::to_string(angleDegrees) + " degrees");
        }
      }
    }
  }
}

void slidingCrossesCollinearWallJunction() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-0.501f, -1.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 0.0f}, 0);
  simulation.addLine({0.0f, 0.0f}, {0.0f, 10.0f}, 1);

  player->setMovement({1.0f, 2.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not remain against the segmented wall");
  requireNear(player->getCentre().y, 1.0f, 0.002f,
              "Player stuck at a collinear wall junction");
}

void slidingCrossesManyWallJunctionsAtEveryOrientation() {
  constexpr float radius = 6.0f;

  for (int angleDegrees = 0; angleDegrees < 360; angleDegrees += 15) {
    auto angle = static_cast<float>(angleDegrees) * 3.14159265358979323846f / 180.0f;
    wp::Vector2 tangent{std::cos(angle), std::sin(angle)};
    auto normal = tangent.perpendicular();

    WorldCollisionSim simulation;
    auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(
        tangent * -15.0f + normal * (radius + 0.001f), radius);
    auto player = playerOwner.get();
    simulation.addSlidingCollider(std::move(playerOwner));
    for (int segment = -20; segment < 20; ++segment) {
      simulation.addLine(tangent * (static_cast<float>(segment) * 2.0f),
                         tangent * (static_cast<float>(segment + 1) * 2.0f),
                         static_cast<uint32_t>(segment + 20));
    }

    for (int frame = 0; frame < 30; ++frame) {
      player->setMovement(tangent - normal * 0.2f);
      simulation.update(1.0f);
    }

    auto expected = tangent * 15.0f + normal * (radius + 0.001f);
    if (player->getCentre().distanceTo(expected) > 0.03f) {
      throw std::runtime_error(
          "Slide caught on a wall junction at angle " +
          std::to_string(angleDegrees) + " degrees");
    }
  }
}

void slidingTraversesSlightlyKinkedWalls() {
  constexpr float radius = 6.0f;

  for (int angleDegrees = 0; angleDegrees < 360; angleDegrees += 30) {
    auto angle = static_cast<float>(angleDegrees) * 3.14159265358979323846f / 180.0f;
    wp::Vector2 tangent{std::cos(angle), std::sin(angle)};
    auto normal = tangent.perpendicular();

    for (float kink : {0.001f, 0.01f, 0.1f, 0.5f}) {
      for (float direction : {-1.0f, 1.0f}) {
        WorldCollisionSim simulation;
        auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(
            tangent * -20.0f + normal * (radius + 1.0f), radius);
        auto player = playerOwner.get();
        simulation.addSlidingCollider(std::move(playerOwner));

        auto previous = tangent * -30.0f;
        for (int segment = 0; segment < 20; ++segment) {
          auto offset = (segment % 2 == 0 ? kink : -kink) * direction;
          auto next = tangent * (-25.0f + static_cast<float>(segment) * 5.0f) +
                      normal * offset;
          simulation.addLine(previous, next,
                             static_cast<uint32_t>(segment));
          previous = next;
        }

        for (int frame = 0; frame < 60; ++frame) {
          player->setMovement(tangent - normal * 0.2f);
          simulation.update(1.0f);
        }

        if (player->getCentre().dot(tangent) < 35.0f) {
          throw std::runtime_error(
              "Slide caught on a kinked wall at angle " +
              std::to_string(angleDegrees) + " with kink size " +
              std::to_string(kink));
        }
        require(!simulation.colliderIntersects(player),
                "Player penetrated a slightly kinked wall");
      }
    }
  }
}

void glancingMovementSlidesAroundWallEndpoint() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-1.0f, -0.8f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 0.0f}, 0);

  for (int frame = 0; frame < 10; ++frame) {
    player->setMovement({0.2f, 0.2f});
    simulation.update(1.0f);
  }

  require(player->getCentre().x > 0.45f,
          "Player stuck on a wall endpoint instead of moving past it");
  require(player->getCentre().y > 0.5f,
          "Player stuck on a wall endpoint instead of sliding around it");
  require(!simulation.colliderIntersects(player),
          "Player penetrated the wall endpoint while sliding around it");
}

void shallowSlidesClearWallEndpoints() {
  constexpr float radius = 6.0f;
  constexpr int frameCount = 2500;

  for (float tangentialMovement : {0.01f, 0.02f, 0.05f, 0.1f, 0.5f}) {
    WorldCollisionSim simulation;
    auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-radius - 0.001f, -12.0f}, radius);
    auto player = playerOwner.get();
    simulation.addSlidingCollider(std::move(playerOwner));
    simulation.addLine({0.0f, -100.0f}, {0.0f, 0.0f}, 0);

    for (int frame = 0; frame < frameCount; ++frame) {
      player->setMovement({1.0f, tangentialMovement});
      simulation.update(1.0f);
    }

    if (player->getCentre().x < 100.0f) {
      throw std::runtime_error(
          "Shallow slide caught at a wall endpoint with tangential movement " +
          std::to_string(tangentialMovement));
    }
    require(!simulation.colliderIntersects(player),
            "Shallow slide penetrated a wall endpoint");
  }
}

void slidingClearsConvexCorners() {
  constexpr float radius = 6.0f;

  for (int turnDegrees : {5, 15, 30, 60, 85}) {
    auto angle = -static_cast<float>(turnDegrees) * 3.14159265358979323846f / 180.0f;
    wp::Vector2 outgoing{std::cos(angle), std::sin(angle)};

    WorldCollisionSim simulation;
    auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-10.0f, radius + 0.001f}, radius);
    auto player = playerOwner.get();
    simulation.addSlidingCollider(std::move(playerOwner));
    simulation.addLine({-100.0f, 0.0f}, {0.0f, 0.0f}, 0);
    simulation.addLine({0.0f, 0.0f}, outgoing * 100.0f, 1);

    for (int frame = 0; frame < 30; ++frame) {
      player->setMovement({1.0f, -0.2f});
      simulation.update(1.0f);
    }

    if (player->getCentre().x < 15.0f) {
      throw std::runtime_error(
          "Slide caught on a convex " + std::to_string(turnDegrees) +
          "-degree corner");
    }
    require(!simulation.colliderIntersects(player),
            "Player penetrated a convex corner while sliding past it");
  }
}

void slidingStopsAtCornerWithoutPenetration() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-2.0f, 0.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, 0);
  simulation.addLine({10.0f, 2.0f}, {-10.0f, 2.0f}, 1);

  player->setMovement({3.0f, 3.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player penetrated the vertical corner wall");
  requireNear(player->getCentre().y, 1.499f, 0.002f,
              "Player penetrated the horizontal corner wall");
}

void diagonalMovementSlidesBothWaysAlongWall() {
  WorldCollisionSim simulation;
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-2.0f, 0.0f}, 0.5f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, 0);

  player->setMovement({3.0f, -2.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, -2.0f, 0.002f,
              "Player did not preserve downward movement along the wall");
}

void nearZeroContactUsesWallNormal() {
  WorldCollisionSim simulation;
  wp::Vector2 const lineStart{-1.422804f, -2.0411403f};
  wp::Vector2 const lineEnd{-1.7840316f, 3.788934f};
  auto playerOwner = std::make_unique<wp::collide::ColliderCircle>(wp::Vector2{-1.5509014f, 0.02642727f}, 0.0f);
  auto player = playerOwner.get();
  simulation.addSlidingCollider(std::move(playerOwner));
  simulation.addLine(lineStart, lineEnd, 0);

  wp::collide::SweepResult result;
  require(simulation.projectCollider(player, {-6.7236485f, -0.41659293f}, &result),
          "The player must contact the wall");
  require(result.newPosition.distanceTo(result.newPosition.closestPointOnLine(lineStart, lineEnd)) > 0.0005f,
          "A near-zero contact normal must fall back to the wall normal");
}
}  // namespace

int main() {
  try {
    generatedBorderZonesFollowResolvedMovement();
    orderedZoneCrossingsAndFrameBoundaries();
    playerLocationUsesResolvedPosition();
    generatedWallsSlideFromThePlayableSide();
    callerCulledWorldLinesDoNotCreateASecondSpatialGrid();
    directMovementProducesOneResolvedSegment();
    diagonalMovementSlidesAlongWall();
    perpendicularMovementStopsAtWall();
    smallMovementStillSlidesAlongWall();
    straightWallSlidingIsRotationInvariant();
    slidingCrossesCollinearWallJunction();
    slidingCrossesManyWallJunctionsAtEveryOrientation();
    slidingTraversesSlightlyKinkedWalls();
    glancingMovementSlidesAroundWallEndpoint();
    shallowSlidesClearWallEndpoints();
    slidingClearsConvexCorners();
    slidingStopsAtCornerWithoutPenetration();
    diagonalMovementSlidesBothWaysAlongWall();
    nearZeroContactUsesWallNormal();
    std::cout << "World wall collision response passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
