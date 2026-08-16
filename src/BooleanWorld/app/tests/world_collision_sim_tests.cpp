#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <willpower/collide/ColliderCircle.h>

#include "WorldCollisionSim.h"

namespace {
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
    slidingStopsAtCornerWithoutPenetration();
    diagonalMovementSlidesBothWaysAlongWall();
    std::cout << "World wall collision response passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
