#include "WorldCollisionSim.h"

#include <algorithm>
#include <stdexcept>

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
        if (line.getUserData() <= ZoneLineBase) {
          if (result->movementDesired.lengthSq() == 0.0f || !mZoneCrossed) return false;
          auto index = uint32_t(ZoneLineBase - line.getUserData());
          mRemainingFrameFraction *= 1.0f - t;
          mZoneCrossed(result->oldPosition,
              result->oldPosition + result->movementDesired, index);
          result->newPosition = result->oldPosition + result->movementDesired * t;
          result->movementDone = result->newPosition - result->oldPosition;
          result->distanceMoved = result->movementDone.length();
          result->movementLeft = result->movementDesired * (1.0f - t);
          recordMovementCandidate(*result, false);
          return true;
        }
        if (line.getUserData() == -1 && mBoundaryApplies && !mBoundaryApplies()) return false;
        if (line.getUserData() != -1 && mIgnoreWorldGeometry && mIgnoreWorldGeometry()) return false;
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
            mRemainingFrameFraction *= 1.0f - std::clamp(
                result->distanceMoved / result->movementDesired.length(), 0.0f, 1.0f);
            // A Portal is relocation rather than a swept crossing. Its exit
            // can therefore jump entirely over the enclosing lines; constrain
            // it before Willpower recursively consumes the remaining motion.
            result->newPosition =
                constrainToMovementBoundary(result->newPosition);
            result->movementDone = result->newPosition - result->oldPosition;
            recordMovementCandidate(*result, true);
            return true;
          }
        }
        if (onWallHit) {
          onWallHit();
        }

        mRemainingFrameFraction *= 1.0f - t;
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

wp::Vector2 WorldCollisionSim::constrainToMovementBoundary(
    wp::Vector2 const& position) const {
  if (!mMovementBoundary || !mPlayerCollider || (mBoundaryApplies && !mBoundaryApplies())) return position;

  auto const halfSize = mPlayerCollider->getBounds().getHalfSize();
  auto minimum = mMovementBoundary->getMinExtent() + halfSize;
  auto maximum = mMovementBoundary->getMaxExtent() - halfSize;
  // A malformed boundary smaller than the player still has a deterministic
  // safe centre rather than reversing the clamp range.
  if (minimum.x > maximum.x) {
    minimum.x = maximum.x = mMovementBoundary->getCentre().x;
  }
  if (minimum.y > maximum.y) {
    minimum.y = maximum.y = mMovementBoundary->getCentre().y;
  }
  return {std::clamp(position.x, minimum.x, maximum.x),
          std::clamp(position.y, minimum.y, maximum.y)};
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
       portalRelocation, mIgnoreWorldGeometry && mIgnoreWorldGeometry()});
}

void WorldCollisionSim::finishMovementTrace() {
  if (!mPlayerCollider) return;

  auto const finalPosition = mPlayerCollider->getCentre();
  auto cursor = mUpdateStart;
  bool physicallyAbsent = mUpdatePhysicallyAbsent;
  auto samePosition = [](wp::Vector2 const& a, wp::Vector2 const& b) {
    return a.distanceTo(b) <= 1.0e-5f;
  };
  auto append = [&](wp::Vector2 const& from, wp::Vector2 const& to,
                    PlayerMovementSegmentType type) {
    if (type == PlayerMovementSegmentType::PortalRelocation ||
        !samePosition(from, to)) {
      mPlayerMovementTrace.push_back({from, to, type, physicallyAbsent});
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
    physicallyAbsent = candidate.physicallyAbsentAfter;
  }

  append(cursor, finalPosition, PlayerMovementSegmentType::Swept);
}

void WorldCollisionSim::update(float frameTime) {
  mPlayerMovementTrace.clear();
  mMovementCandidates.clear();
  mRemainingFrameFraction = 1.0f;
  if (!mPlayerCollider) {
    collide::Simulation::update(frameTime);
    return;
  }

  mUpdateStart = mPlayerCollider->getCentre();
  mUpdatePhysicallyAbsent = mIgnoreWorldGeometry && mIgnoreWorldGeometry();
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

void WorldCollisionSim::updateWithinBoundary(
    float frameTime, wp::BoundingBox const& boundary) {
  auto const firstBoundaryLine = mStaticLines.size();
  mMovementBoundary = boundary;
  auto const minimum = boundary.getMinExtent();
  auto const maximum = boundary.getMaxExtent();
  // -1 is an engine collision line, distinct from Portal lines (<= -2) and
  // from every generated ArrangementWall index (>= 0).
  mStaticLines.push_back({{minimum.x, minimum.y}, {maximum.x, minimum.y},
                          true, 1.0f, -1});
  mStaticLines.push_back({{maximum.x, minimum.y}, {maximum.x, maximum.y},
                          true, 1.0f, -1});
  mStaticLines.push_back({{maximum.x, maximum.y}, {minimum.x, maximum.y},
                          true, 1.0f, -1});
  mStaticLines.push_back({{minimum.x, maximum.y}, {minimum.x, minimum.y},
                          true, 1.0f, -1});

  if (mPlayerCollider) {
    mPlayerCollider->_setPosition(
        constrainToMovementBoundary(mPlayerCollider->getCentre()));
  }
  try {
    update(frameTime);
  } catch (...) {
    mStaticLines.resize(firstBoundaryLine);
    mMovementBoundary.reset();
    throw;
  }
  mStaticLines.resize(firstBoundaryLine);
  mMovementBoundary.reset();
}

vector<WorldCollisionSim::PlayerMovementSegment> const&
WorldCollisionSim::getPlayerMovementTrace() const {
  return mPlayerMovementTrace;
}

bool WorldCollisionSim::sweepAgainstStaticLine(
    wp::collide::Collider const* collider,
    wp::Vector2 const& desiredPosition,
    wp::collide::StaticLine const& line, float* time) const {
  if (line.getUserData() <= ZoneLineBase) {
    if (!mZoneQuery) return false;
    auto crossing = mZoneQuery(collider->getCentre(), desiredPosition,
        uint32_t(ZoneLineBase - line.getUserData()));
    if (!crossing) return false;
    *time = float(*crossing);
    return true;
  }
  if (line.getUserData() == -1 && mBoundaryApplies && !mBoundaryApplies()) return false;
  if (line.getUserData() != -1 && mIgnoreWorldGeometry && mIgnoreWorldGeometry()) return false;
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

void WorldCollisionSim::setZoneCallbacks(
    std::function<std::optional<double>(wp::Vector2 const&, wp::Vector2 const&, uint32_t)> query,
    std::function<void(wp::Vector2 const&, wp::Vector2 const&, uint32_t)> crossed,
    std::function<bool()> ignoreWorldGeometry, std::function<bool()> boundaryApplies) {
  mZoneQuery = std::move(query);
  mZoneCrossed = std::move(crossed);
  mIgnoreWorldGeometry = std::move(ignoreWorldGeometry);
  mBoundaryApplies = std::move(boundaryApplies);
}

void WorldCollisionSim::addZoneLine(wp::Vector2 const& v0, wp::Vector2 const& v1, uint32_t wall) {
  if (wall >= uint32_t(-ZoneLineBase)) throw std::invalid_argument("Too many Zone walls");
  mStaticLines.push_back({v0, v1, true, 1.0f, ZoneLineBase - int32_t(wall)});
}

void WorldCollisionSim::updatePhantom(float frameTime, wp::BoundingBox const& boundary) {
  auto previous = std::move(mIgnoreWorldGeometry);
  mIgnoreWorldGeometry = [] { return true; };
  try { updateWithinBoundary(frameTime, boundary); }
  catch (...) { mIgnoreWorldGeometry = std::move(previous); throw; }
  mIgnoreWorldGeometry = std::move(previous);
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
