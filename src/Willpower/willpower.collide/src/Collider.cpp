#include <cassert>

#include "willpower/collide/Collider.h"

// Implemented types
#include "willpower/collide/ColliderCircle.h"
#include "willpower/collide/ColliderAABB.h"

using namespace std;

namespace WP_NAMESPACE {
namespace collide {

using namespace WP_NAMESPACE;

Collider::Collider(Type type)
    : mIndex(-1), mType(type), mMovement(0, 0), mConsumeMovement(false), mHitLineCallback({}) {
}

void Collider::_setIndex(int32_t index) {
  mIndex = index;
}

int32_t Collider::getIndex() const {
  return mIndex;
}

Collider::Type Collider::getType() const {
  return mType;
}

Vector2 const& Collider::getCentre() const {
  return mBounds.getCentre();
}

void Collider::setBounds(BoundingBox const& box) {
  mBounds = box;
}

void Collider::setBounds(float x, float y, float width, float height) {
  mBounds.setPosition(x, y);
  mBounds.setSize(width, height);
}

void Collider::setBounds(Vector2 const& position, Vector2 const& size) {
  setBounds(position.x, position.y, size.x, size.y);
}

BoundingBox const& Collider::getBounds() const {
  return mBounds;
}

void Collider::setHitLineCallback(HitLineCallback callback) {
  mHitLineCallback = callback;
}

bool Collider::fireHitLineCallback(SweepResult* result, StaticLine const& line, float t, void* userObj) const {
  if (mHitLineCallback) {
    return mHitLineCallback(result, line, t, userObj);
  } else {
    return false;
  }
}

bool Collider::pointInside(Vector2 const& point) const {
  return pointInside(point.x, point.y);
}

void Collider::setMovement(float x, float y) {
  mMovement.set(x, y);
}

void Collider::setMovement(Vector2 const& movement) {
  setMovement(movement.x, movement.y);
}

Vector2 const& Collider::getMovement() const {
  return mMovement;
}

void Collider::setConsumeMovement(bool consume) {
  mConsumeMovement = consume;
}

bool Collider::isMovementConsumed() const {
  return mConsumeMovement;
}

void Collider::_consumeMovement(Vector2 const& movement) {
  setMovement(mMovement - movement);
}

void Collider::checkCollision(Collider* a, Collider* b) {
  int typeCode = b->getType() * Type::MAX_TYPES + a->getType();

  switch (typeCode) {
    case 0:
      // Circle-circle collision
      // BoundingCircle const& objectA = static_cast<ColliderCircle*>(a)->getObject();
      // BoundingCircle const& objectB = static_cast<ColliderCircle*>(b)->getObject();
      break;

    case 1:
      // Circle-AABB collision
      // BoundingCircle const& objectA = static_cast<ColliderCircle*>(a)->getObject();
      // BoundingBox const& objectB = static_cast<ColliderAABB*>(b)->getObject();
      break;

    case 8:
      // AABB-circle collision
      // BoundingBox const& objectA = static_cast<ColliderAABB*>(a)->getObject();
      // BoundingCircle const& objectB = static_cast<ColliderCircle*>(b)->getObject();
      break;

    case 9:
      // AABB-AABB collision
      // BoundingBox const& objectA = static_cast<ColliderAABB*>(a)->getObject();
      // BoundingBox const& objectB = static_cast<ColliderAABB*>(b)->getObject();
      break;

    default:
      break;
  }
}

}  // namespace collide
}  // namespace WP_NAMESPACE
