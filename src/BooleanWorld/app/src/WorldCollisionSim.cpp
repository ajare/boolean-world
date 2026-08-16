#include <willpower/common/ExtentsCalculator.h>

#include "WorldCollisionSim.h"

using namespace std;
using namespace wp;

WorldCollisionSim::WorldCollisionSim(void* userObj)
    : collide::Simulation(ExtentsCalculator({0.0f, 0.0f}, {100.0f, 100.f}, 0.0f), 1, 1, userObj) {
}

void WorldCollisionSim::addSlidingCollider(
    wp::collide::Collider* collider,
    std::function<void()> const& onWallHit) {
  collider->setHitLineCallback(
      [onWallHit](wp::collide::SweepResult* result,
                  wp::collide::StaticLine const& line,
                  float t,
                  void*) {
        if (onWallHit) {
          onWallHit();
        }

        auto contactPosition =
            result->oldPosition + result->movementDesired * t;
        auto closestPoint = contactPosition.closestPointOnLine(
            line.getVertex(0), line.getVertex(1));
        auto normal = contactPosition - closestPoint;
        if (normal.normalise() <= 1e-8) {
          normal = line.getNormal();
          if (result->movementDesired.dot(normal) > 0.0f) {
            normal = -normal;
          }
        }

        result->newPosition = contactPosition + normal * 0.001f;
        result->movementDone = result->newPosition - result->oldPosition;
        result->distanceMoved = result->movementDone.length();

        auto movementAfterContact = result->movementDesired * (1.0f - t);
        auto inwardMovement = movementAfterContact.dot(normal);
        result->movementLeft = inwardMovement < 0.0f
                                   ? movementAfterContact - normal * inwardMovement
                                   : movementAfterContact;
        return true;
      });
  addCollider(collider);
}

set<uint32_t> WorldCollisionSim::getLineIndices(BoundingBox const& bounds) const {
  set<uint32_t> indices;

  auto numLines = getNumStaticLines();

  for (uint32_t i = 0; i < numLines; ++i) {
    indices.insert(i);
  }

  return indices;
}

vector<wp::collide::StaticLine> const& WorldCollisionSim::getLines() const {
  return mStaticLines;
}

void WorldCollisionSim::clearLines() {
  mStaticLines.clear();
}

void WorldCollisionSim::addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint32_t index) {
  mStaticLines.push_back({v0, v1, true, 1.0f, (int32_t)index});
}
