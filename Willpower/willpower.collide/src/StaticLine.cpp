#include "willpower/collide/StaticLine.h"

using namespace std;

namespace WP_NAMESPACE {
namespace collide {

using namespace WP_NAMESPACE;

StaticLine::StaticLine()
    : mDoubleSided(false), mFriction(1.0f), mUserData(-1), mEnabled(true) {
}

StaticLine::StaticLine(Vector2 const& v0, Vector2 const& v1, bool doubleSided, float friction, int32_t userData)
    : mVertices{v0, v1}, mDoubleSided(doubleSided), mFriction(friction), mUserData(userData), mEnabled(true) {
  mNormal = (mVertices[1] - mVertices[0]).perpendicular().normalisedCopy();
}

Vector2 const& StaticLine::getNormal() const {
  return mNormal;
}

Vector2 const& StaticLine::getVertex(int index) const {
  return mVertices[index];
}

void StaticLine::getVertices(Vector2& v0, Vector2& v1) const {
  v0 = mVertices[0];
  v1 = mVertices[1];
}

bool StaticLine::isDoubleSided() const {
  return mDoubleSided;
}

float StaticLine::getFriction() const {
  return mFriction;
}

int32_t StaticLine::getUserData() const {
  return mUserData;
}

void StaticLine::enable(bool enable) {
  mEnabled = enable;
}

bool StaticLine::enabled() const {
  return mEnabled;
}
}  // namespace collide
}  // namespace WP_NAMESPACE
