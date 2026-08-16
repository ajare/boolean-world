#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <willpower/collide/ColliderCircle.h>
#include <willpower/collide/Simulation.h>
#include <willpower/common/ExtentsCalculator.h>

namespace {

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

class TrackingColliderCircle : public wp::collide::ColliderCircle {
  int32_t mId;
  std::vector<int32_t>& mSweepOrder;

public:
  TrackingColliderCircle(wp::Vector2 const& position, int32_t id,
                         std::vector<int32_t>& sweepOrder)
      : wp::collide::ColliderCircle(position, 0.5f),
        mId(id),
        mSweepOrder(sweepOrder) {
  }

  bool sweepAgainstLine(wp::Vector2 const& target, wp::Vector2 const& linev0,
                        wp::Vector2 const& linev1, float* t) const override {
    mSweepOrder.push_back(mId);
    return wp::collide::ColliderCircle::sweepAgainstLine(target, linev0, linev1,
                                                         t);
  }
};

void collidersResolveInInsertionOrderWhenAddressesAreReversed() {
  auto simulation = createSimulation();
  simulation.addStaticLine({5.0f, 0.0f}, {5.0f, 10.0f}, false);

  std::vector<int32_t> sweepOrder;
  auto colliderA = std::make_unique<TrackingColliderCircle>(wp::Vector2(2.0f, 3.0f), 0, sweepOrder);
  auto colliderB = std::make_unique<TrackingColliderCircle>(wp::Vector2(2.0f, 7.0f), 1, sweepOrder);

  std::unique_ptr<TrackingColliderCircle> firstCollider;
  std::unique_ptr<TrackingColliderCircle> secondCollider;
  std::vector<int32_t> expectedOrder;
  if (std::less<wp::collide::Collider*>{}(colliderA.get(), colliderB.get())) {
    firstCollider = std::move(colliderB);
    secondCollider = std::move(colliderA);
    expectedOrder = {1, 0};
  } else {
    firstCollider = std::move(colliderA);
    secondCollider = std::move(colliderB);
    expectedOrder = {0, 1};
  }

  firstCollider->setMovement({5.0f, 0.0f});
  secondCollider->setMovement({5.0f, 0.0f});

  simulation.addCollider(std::move(firstCollider));
  simulation.addCollider(std::move(secondCollider));
  simulation.update(1.0f);

  require(sweepOrder == expectedOrder,
          "Colliders must resolve in insertion order, not pointer order");
}

}  // namespace

int main() {
  try {
    collidersResolveInInsertionOrderWhenAddressesAreReversed();
    std::cout << "Simulation order passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
