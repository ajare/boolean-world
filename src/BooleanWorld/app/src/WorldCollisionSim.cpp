#include "WorldCollisionSim.h"

using namespace std;
using namespace wp;

WorldCollisionSim::WorldCollisionSim(void* userObj)
    : collide::Simulation(userObj) {
  // StatePlayBooleanWorld narrows the active walls through ArrangementWorldData's
  // wall grid before adding them, so a second spatial grid here would be unused.
}

void WorldCollisionSim::addSlidingCollider(
    unique_ptr<wp::collide::Collider> collider,
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
  addCollider(move(collider));
}

void WorldCollisionSim::getLineIndices(
    BoundingBox const&,
    vector<uint32_t>& indices) const {
  indices.clear();
  auto const numLines = getNumStaticLines();
  indices.reserve(numLines);
  for (uint32_t i = 0; i < numLines; ++i) {
    indices.push_back(i);
  }
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
