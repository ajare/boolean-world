#pragma once

#include <vector>
#include <functional>

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Mesh.h"
#include "willpower/firepower/ControlSetting.h"

namespace WP_NAMESPACE {
namespace firepower {
class MeshCollisionManager;

class WP_FIREPOWER_API Bomb {
public:
  typedef std::pair<float, float> Arc;

public:
  struct Line {
    wp::Vector2 end;
    float angle;
    int startEnd;
  };

  struct LifeOptions {
    float maxRadius;
    float lifetime;

    LifeOptions()
        : maxRadius(-1.0f), lifetime(-1.0f) {
    }
  };

private:
  wp::Vector2 mPosition;

  float mAcceleration;

  float mSpeed;

  float mRotation;

  float mRadius;

  float mDepth;

  float mLifeTime;

  float mMaxRadius;

  ControlFunction mGetAccelerationFn;

  ControlFunction mGetSpeedFn;

  ControlFunction mGetRotationFn;

  bool mClipArcs;

  // Working variables
  bool mHasHit;

  std::vector<Arc> mArcs, mResultArcs;

private:
  bool clipSlabs(float x0, float x1, float y0, float y1, float* a, float* b);

  void clipArcs(Arc const& a, Arc const& b, std::vector<Arc>& clipped);

public:
  Bomb();

  void initialise(Vector2 const& position, float radius, float depth, bool clipArcs, LifeOptions const& lifeOptions, ControlSetting accelValue, ControlSetting speedValue, ControlSetting rotateValue, std::vector<Arc> const& arcs);

  wp::Vector2 const& getPosition() const;

  float getRadius() const;

  float getDepth() const;

  std::vector<Bomb::Arc> const& getArcs() const;

  bool update(MeshCollisionManager const* meshCollisionMgr, float frameTime);
};

}  // namespace firepower
}  // namespace WP_NAMESPACE
