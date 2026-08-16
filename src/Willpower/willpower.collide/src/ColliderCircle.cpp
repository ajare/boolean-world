#include <cassert>

#include "willpower/common/MathsUtils.h"

#include "willpower/collide/ColliderCircle.h"

using namespace std;

namespace WP_NAMESPACE {
namespace collide {

using namespace WP_NAMESPACE;

ColliderCircle::ColliderCircle(float x, float y, float radius)
    : Collider(Type::Circle), mObject(x, y, radius) {
  setBounds(x - radius, y - radius, radius * 2, radius * 2);
}

ColliderCircle::ColliderCircle(Vector2 const& position, float radius)
    : ColliderCircle(position.x, position.y, radius) {
}

ColliderCircle::ColliderCircle(BoundingCircle const& bounds)
    : Collider(Type::Circle), mObject(bounds) {
  auto const& pos = bounds.getPosition();
  auto radius = bounds.getRadius();
  setBounds(pos.x - radius, pos.y - radius, radius * 2, radius * 2);
}

void ColliderCircle::_setPosition(Vector2 const& position) {
  mObject.setPosition(position);

  float radius = mObject.getRadius();
  setBounds(position.x - radius, position.y - radius, radius * 2, radius * 2);
}

BoundingCircle const& ColliderCircle::getObject() const {
  return mObject;
}

bool ColliderCircle::pointInside(float x, float y) const {
  return mObject.pointInside(x, y);
}

bool ColliderCircle::sweepAgainstLine(Vector2 const& target, Vector2 const& linev0, Vector2 const& linev1, float* t) const {
  return MathsUtils::sweepCircleAgainstLine(
      getBounds().getCentre(),
      getObject().getRadius(),
      target,
      linev0,
      linev1,
      t);
}

bool ColliderCircle::intersectsLine(Vector2 const& linev0, Vector2 const& linev1) const {
  return MathsUtils::lineIntersectsCircle(linev0, linev1, mObject.getPosition(), mObject.getRadius()) != MathsUtils::LineIntersectionType::NotIntersecting;
}

}  // namespace collide
}  // namespace WP_NAMESPACE
