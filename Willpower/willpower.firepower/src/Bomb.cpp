#include <algorithm>

#include <utils/StringUtils.h>

#include "willpower/common/MathsUtils.h"

#include "willpower/firepower/Bomb.h"
#include "willpower/firepower/MeshCollisionManager.h"

#define EQ_EPSILON(x, y) (x >= ((y) - MathsUtils::Epsilon) && x <= ((y) + MathsUtils::Epsilon))
#define LTE_EPSILON(x, y) (x <= ((y) + MathsUtils::Epsilon))
#define GTE_EPSILON(x, y) (x >= ((y) - MathsUtils::Epsilon))

namespace WP_NAMESPACE {
namespace firepower {
using namespace std;

Bomb::Bomb()
    : mPosition(Vector2::ZERO), mAcceleration(0.0f), mSpeed(0.0f), mRotation(0.0f), mRadius(0.0f), mDepth(0.0f), mLifeTime(-1.0f), mMaxRadius(-1.0f), mGetAccelerationFn({}), mGetSpeedFn({}), mGetRotationFn({}), mArcs(vector<Arc>()), mHasHit(false), mClipArcs(false) {
}

void Bomb::initialise(wp::Vector2 const& position, float radius, float depth, bool clipArcs, LifeOptions const& lifeOptions, ControlSetting accelValue, ControlSetting speedValue, ControlSetting rotateValue, vector<Arc> const& arcs) {
  mHasHit = false;

  // Mandatory
  mPosition = position;
  mRadius = radius;
  mClipArcs = clipArcs;

  // Optional
  mDepth = depth;
  mLifeTime = lifeOptions.lifetime;
  mMaxRadius = lifeOptions.maxRadius;

  // Acceleration
  if (accelValue.option == ControlOptions::Function) {
    mGetAccelerationFn = accelValue.function;
    mAcceleration = 0.0f;
  } else if (accelValue.option == ControlOptions::Scalar) {
    mGetAccelerationFn = {};
    mAcceleration = accelValue.scalar;
  } else {
    mGetAccelerationFn = {};
    mAcceleration = 0.0f;
  }

  // Speed
  if (speedValue.option == ControlOptions::Function) {
    mGetSpeedFn = speedValue.function;
    mSpeed = 0.0f;
  } else if (speedValue.option == ControlOptions::Scalar) {
    mGetSpeedFn = {};
    mSpeed = speedValue.scalar;
  } else {
    mGetSpeedFn = {};
    mSpeed = 0.0f;
  }

  // Rotation
  if (rotateValue.option == ControlOptions::Function) {
    mGetRotationFn = rotateValue.function;
    mRotation = 0.0f;
  } else if (rotateValue.option == ControlOptions::Scalar) {
    mGetRotationFn = {};
    mRotation = rotateValue.scalar;
  } else {
    mGetRotationFn = {};
    mRotation = 0.0f;
  }

  // Arcs
  mArcs = arcs;

  sort(mArcs.begin(), mArcs.end(), [](Arc const& a, Arc const& b) {
    if (a.first < b.first) {
      return true;
    } else if (a.first > b.first) {
      return false;
    } else {
      return a.second < b.second;
    }
  });
}

Vector2 const& Bomb::getPosition() const {
  return mPosition;
}

float Bomb::getRadius() const {
  return mRadius;
}

float Bomb::getDepth() const {
  return mDepth;
}

vector<Bomb::Arc> const& Bomb::getArcs() const {
  return mResultArcs;
}

bool Bomb::clipSlabs(float x0, float x1, float y0, float y1, float* a, float* b) {
  *a = max(x0, y0);
  *b = min(x1, y1);
  return *a < *b;
}

void Bomb::clipArcs(Arc const& a, Arc const& b, vector<Arc>& clipped) {
  float p0, p1;

  if (a.first < a.second) {
    if (b.first < b.second) {
      if (clipSlabs(a.first, a.second, b.first, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
      }
    } else {
      if (clipSlabs(a.first, a.second, b.first, 360, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
      }
      if (clipSlabs(a.first, a.second, 0, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
      }
    }
  } else {
    if (b.first < b.second) {
      if (clipSlabs(a.first, 360, b.first, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
      }
      if (clipSlabs(0, a.second, b.first, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
      }
    } else {
      int added = 0;
      if (clipSlabs(a.first, 360, b.first, 360, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
        added++;
      }
      if (clipSlabs(a.first, 360, 0, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
        added++;
      }
      if (clipSlabs(0, a.second, b.first, 360, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
        added++;
      }
      if (clipSlabs(0, a.second, 0, b.second, &p0, &p1)) {
        clipped.push_back(make_pair(p0, p1));
        added++;
      }

      if (added == 2) {
        size_t size = clipped.size();
        if (clipped[size - 2].second == 360 && clipped[size - 1].first == 0) {
          clipped[size - 2].second = clipped[size - 1].second;
          clipped.pop_back();
        }
      }
    }
  }
}

bool Bomb::update(MeshCollisionManager const* meshCollisionMgr, float frameTime) {
  // Check for life
  if (mLifeTime > 0) {
    mLifeTime -= frameTime;
    if (mLifeTime <= 0.0f) {
      return false;
    }
  }

  // Update acceleration
  if (mGetAccelerationFn) {
    mAcceleration = mGetAccelerationFn(frameTime, nullptr);
  }

  // Update speed
  if (mGetSpeedFn) {
    mSpeed = mGetSpeedFn(frameTime, nullptr);
  } else {
    mSpeed += mAcceleration * frameTime;
  }

  // Update radius
  mRadius += mSpeed * frameTime;

  // Check for max radius
  if (mMaxRadius > 0) {
    if (mRadius > mMaxRadius) {
      return false;
    }
  }

  // Collision
  // auto arcs = intersect(mesh);
  auto arcs = meshCollisionMgr->checkBomb(*this);

  if (!arcs.empty()) {
    mHasHit = true;
  }

  if (arcs.empty() && !mHasHit) {
    Arc newArc;
    newArc.first = 0.0f;
    newArc.second = 360.0f;

    arcs.push_back(newArc);
  }

  // Intersect with mArcs
  if (!mArcs.empty()) {
    // for each arc in mArcs, clip against each arc in arcs
    vector<Arc> clipped;
    float rotate = (mGetRotationFn ? mGetRotationFn(frameTime, nullptr) : mRotation) * frameTime;
    for (auto& arc : mArcs) {
      // Rotate
      arc.first += rotate;
      while (arc.first < 0.0f) arc.first += 360.0f;
      while (arc.first >= 360.0f) arc.first -= 360.0f;

      arc.second += rotate;
      while (arc.second < 0.0f) arc.second += 360.0f;
      while (arc.second >= 360.0f) arc.second -= 360.0f;

      for (auto& clipArc : arcs) {
        while (clipArc.first >= 360.0f) {
          clipArc.first -= 360.0f;
        }

        while (clipArc.second >= 360.0f) {
          clipArc.second -= 360.0f;
        }

        clipArcs(arc, clipArc, clipped);
      }
    }

    mResultArcs = clipped;

    if (mClipArcs) {
      mArcs = clipped;
    }
  } else {
    mResultArcs = arcs;
  }

  return !mResultArcs.empty();
}

}  // namespace firepower
}  // namespace WP_NAMESPACE
