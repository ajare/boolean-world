#include "WorldCollisionSim.h"

#include <algorithm>

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
  mPlayerCollider = collider.get();
  collider->setHitLineCallback(
      [this, onWallHit](wp::collide::SweepResult* result,
                        wp::collide::StaticLine const& line,
                        float t,
                        void*) {
        if (line.getUserData() <= -2 && mPortalHitCallback) {
          // Willpower also calls this callback with an empty movement for its
          // post-sweep overlap check. An aperture may legitimately overlap the
          // collider during approach/emergence; only a swept crossing decides
          // whether it blocks. Do not mutate traversal state during that query.
          if (result->movementDesired.lengthSq() == 0.0f) return false;
          auto portalLineIndex = uint32_t(-2 - line.getUserData());
          auto response = mPortalHitCallback(result, portalLineIndex);
          if (response == PortalLineResponse::Ignore) return false;
          if (response == PortalLineResponse::Traverse) {
            recordMovementCandidate(*result, true);
            return true;
          }
        }
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
        recordMovementCandidate(*result, false);
        return true;
      });
  addCollider(move(collider));
}

void WorldCollisionSim::recordMovementCandidate(
    wp::collide::SweepResult const& result, bool portalRelocation) {
  if (!mTracingUpdate || result.movementDesired.lengthSq() == 0.0f) return;

  auto portalSource = result.newPosition;
  if (portalRelocation) {
    auto desiredLength = result.movementDesired.length();
    auto distance = std::clamp(result.distanceMoved, 0.0f, desiredLength);
    portalSource = result.oldPosition;
    if (desiredLength > 0.0f) {
      portalSource += result.movementDesired * (distance / desiredLength);
    }
  }
  mMovementCandidates.push_back(
      {result.oldPosition, result.newPosition, portalSource,
       portalRelocation});
}

void WorldCollisionSim::finishMovementTrace() {
  if (!mPlayerCollider) return;

  auto const finalPosition = mPlayerCollider->getCentre();
  auto cursor = mUpdateStart;
  auto samePosition = [](wp::Vector2 const& a, wp::Vector2 const& b) {
    return a.distanceTo(b) <= 1.0e-5f;
  };
  auto append = [&](wp::Vector2 const& from, wp::Vector2 const& to,
                    PlayerMovementSegmentType type) {
    if (type == PlayerMovementSegmentType::PortalRelocation ||
        !samePosition(from, to)) {
      mPlayerMovementTrace.push_back({from, to, type});
    }
  };

  for (size_t i = 0; i < mMovementCandidates.size(); ++i) {
    auto const& candidate = mMovementCandidates[i];
    if (!samePosition(candidate.oldPosition, cursor)) continue;

    auto followedByRecursiveSweep =
        i + 1 < mMovementCandidates.size() &&
        samePosition(mMovementCandidates[i + 1].oldPosition,
                     candidate.newPosition);
    // Simulation restores oldPosition when its post-sweep overlap check
    // rejects a candidate. A following recursive sweep or any different final
    // position proves that this candidate was accepted.
    if (!followedByRecursiveSweep &&
        samePosition(finalPosition, candidate.oldPosition)) {
      continue;
    }

    if (candidate.portalRelocation) {
      append(candidate.oldPosition, candidate.portalSourcePosition,
             PlayerMovementSegmentType::Swept);
      append(candidate.portalSourcePosition, candidate.newPosition,
             PlayerMovementSegmentType::PortalRelocation);
    } else {
      append(candidate.oldPosition, candidate.newPosition,
             PlayerMovementSegmentType::Swept);
    }
    cursor = candidate.newPosition;
  }

  append(cursor, finalPosition, PlayerMovementSegmentType::Swept);
}

void WorldCollisionSim::update(float frameTime) {
  mPlayerMovementTrace.clear();
  mMovementCandidates.clear();
  if (!mPlayerCollider) {
    collide::Simulation::update(frameTime);
    return;
  }

  mUpdateStart = mPlayerCollider->getCentre();
  mTracingUpdate = true;
  try {
    collide::Simulation::update(frameTime);
  } catch (...) {
    mTracingUpdate = false;
    throw;
  }
  mTracingUpdate = false;
  finishMovementTrace();
}

vector<WorldCollisionSim::PlayerMovementSegment> const&
WorldCollisionSim::getPlayerMovementTrace() const {
  return mPlayerMovementTrace;
}

bool WorldCollisionSim::sweepAgainstStaticLine(
    wp::collide::Collider const* collider,
    wp::Vector2 const& desiredPosition,
    wp::collide::StaticLine const& line, float* time) const {
  // Shape sweeps only report initial contact. Once an allowed approach has
  // overlapped the aperture, keep reporting it until the centre crosses so
  // that small frame movements cannot walk straight through without transport.
  if (line.getUserData() <= -2 &&
      collider->intersectsLine(line.getVertex(0), line.getVertex(1))) {
    *time = 0.0f;
    return true;
  }
  return Simulation::sweepAgainstStaticLine(
      collider, desiredPosition, line, time);
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
  mPortalLineCount = 0;
}

void WorldCollisionSim::setPortalHitCallback(PortalHitCallback callback) {
  mPortalHitCallback = std::move(callback);
}

void WorldCollisionSim::addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint32_t index) {
  mStaticLines.push_back({v0, v1, true, 1.0f, (int32_t)index});
}

uint32_t WorldCollisionSim::addPortalLine(
    wp::Vector2 const& v0, wp::Vector2 const& v1) {
  auto index = mPortalLineCount++;
  mStaticLines.push_back(
      {v0, v1, true, 1.0f, -2 - static_cast<int32_t>(index)});
  return index;
}
