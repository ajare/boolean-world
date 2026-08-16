#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <willpower/collide/ColliderCircle.h>

#include "WorldCollisionSim.h"

namespace {
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

void diagonalMovementSlidesAlongWall() {
  WorldCollisionSim simulation;
  auto player = new wp::collide::ColliderCircle({-2.0f, 0.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, false, 0);

  player->setMovement({3.0f, 2.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 2.0f, 0.002f,
              "Player did not preserve upward movement along the wall");
}

void perpendicularMovementStopsAtWall() {
  WorldCollisionSim simulation;
  auto player = new wp::collide::ColliderCircle({-2.0f, 1.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, false, 0);

  player->setMovement({3.0f, 0.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 1.0f, 0.002f,
              "Perpendicular impact introduced tangential movement");
}

void smallMovementStillSlidesAlongWall() {
  WorldCollisionSim simulation;
  auto player = new wp::collide::ColliderCircle({-0.55f, 0.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, false, 0);

  player->setMovement({0.1f, 0.05f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, 0.05f, 0.002f,
              "Small tangential movement was discarded");
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
        auto player = new wp::collide::ColliderCircle(normal * (radius + 0.001f), radius);
        simulation.addSlidingCollider(player);
        simulation.addLine(tangent * -100.0f, tangent * 100.0f, false, 0);

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
  auto player = new wp::collide::ColliderCircle({-0.501f, -1.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 0.0f}, false, 0);
  simulation.addLine({0.0f, 0.0f}, {0.0f, 10.0f}, false, 1);

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
    auto player = new wp::collide::ColliderCircle(
        tangent * -15.0f + normal * (radius + 0.001f), radius);
    simulation.addSlidingCollider(player);
    for (int segment = -20; segment < 20; ++segment) {
      simulation.addLine(tangent * (static_cast<float>(segment) * 2.0f),
                         tangent * (static_cast<float>(segment + 1) * 2.0f),
                         false, static_cast<uint32_t>(segment + 20));
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
        auto player = new wp::collide::ColliderCircle(
            tangent * -20.0f + normal * (radius + 1.0f), radius);
        simulation.addSlidingCollider(player);

        auto previous = tangent * -30.0f;
        for (int segment = 0; segment < 20; ++segment) {
          auto offset = (segment % 2 == 0 ? kink : -kink) * direction;
          auto next = tangent * (-25.0f + static_cast<float>(segment) * 5.0f) +
                      normal * offset;
          simulation.addLine(previous, next, false,
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
  auto player = new wp::collide::ColliderCircle({-1.0f, -0.8f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 0.0f}, false, 0);

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
    auto player = new wp::collide::ColliderCircle({-radius - 0.001f, -12.0f}, radius);
    simulation.addSlidingCollider(player);
    simulation.addLine({0.0f, -100.0f}, {0.0f, 0.0f}, false, 0);

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
    auto player = new wp::collide::ColliderCircle({-10.0f, radius + 0.001f}, radius);
    simulation.addSlidingCollider(player);
    simulation.addLine({-100.0f, 0.0f}, {0.0f, 0.0f}, false, 0);
    simulation.addLine({0.0f, 0.0f}, outgoing * 100.0f, false, 1);

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
  auto player = new wp::collide::ColliderCircle({-2.0f, 0.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, false, 0);
  simulation.addLine({10.0f, 2.0f}, {-10.0f, 2.0f}, false, 1);

  player->setMovement({3.0f, 3.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player penetrated the vertical corner wall");
  requireNear(player->getCentre().y, 1.499f, 0.002f,
              "Player penetrated the horizontal corner wall");
}

void diagonalMovementSlidesBothWaysAlongWall() {
  WorldCollisionSim simulation;
  auto player = new wp::collide::ColliderCircle({-2.0f, 0.0f}, 0.5f);
  simulation.addSlidingCollider(player);
  simulation.addLine({0.0f, -10.0f}, {0.0f, 10.0f}, false, 0);

  player->setMovement({3.0f, -2.0f});
  simulation.update(1.0f);

  requireNear(player->getCentre().x, -0.501f, 0.002f,
              "Player did not stop at the wall");
  requireNear(player->getCentre().y, -2.0f, 0.002f,
              "Player did not preserve downward movement along the wall");
}
}  // namespace

int main() {
  try {
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
    std::cout << "World wall collision response passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
