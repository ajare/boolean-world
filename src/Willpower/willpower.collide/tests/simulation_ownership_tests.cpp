#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <willpower/collide/ColliderCircle.h>
#include <willpower/collide/Simulation.h>
#include <willpower/common/ExtentsCalculator.h>

namespace {

static_assert(!std::is_copy_constructible_v<wp::collide::Simulation>);
static_assert(!std::is_copy_assignable_v<wp::collide::Simulation>);

class TrackedColliderCircle : public wp::collide::ColliderCircle {
  bool* mDestroyed;

public:
  explicit TrackedColliderCircle(bool* destroyed)
      : wp::collide::ColliderCircle({5.0f, 5.0f}, 1.0f),
        mDestroyed(destroyed) {
  }

  ~TrackedColliderCircle() override {
    *mDestroyed = true;
  }
};

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

wp::collide::Simulation createSimulation() {
  return wp::collide::Simulation(
      wp::ExtentsCalculator(
          wp::Vector2(0.0f, 0.0f), wp::Vector2(10.0f, 10.0f), 0.0f),
      1, 1);
}

void addingColliderTransfersOwnership() {
  bool destroyed = false;
  {
    auto simulation = createSimulation();
    auto collider = std::make_unique<TrackedColliderCircle>(&destroyed);
    auto observer = collider.get();

    simulation.addCollider(std::move(collider));

    require(!collider, "addCollider must consume the collider ownership");
    require(!destroyed, "The transferred collider must remain alive in the simulation");
    auto colliders = simulation.getColliders();
    require(colliders.size() == 1 && colliders.front() == observer,
            "The simulation must expose the transferred collider as an observer");
  }

  require(destroyed, "Destroying the simulation must destroy its collider");
}

void removingColliderReleasesOwnership() {
  bool destroyed = false;
  auto simulation = createSimulation();
  auto collider = std::make_unique<TrackedColliderCircle>(&destroyed);
  auto observer = collider.get();
  simulation.addCollider(std::move(collider));

  simulation.removeCollider(observer);

  require(destroyed, "Removing a collider must destroy the owned collider");
  require(simulation.getNumColliders() == 0,
          "Removing a collider must remove it from the simulation");
}

void sweepStatisticsStartAtZero() {
  auto simulation = createSimulation();
  require(simulation.getNumSweepChecks() == 0,
          "Sweep statistics must be initialized before the first update");
}

}  // namespace

int main() {
  try {
    addingColliderTransfersOwnership();
    removingColliderReleasesOwnership();
    sweepStatisticsStartAtZero();
    std::cout << "Simulation ownership passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
