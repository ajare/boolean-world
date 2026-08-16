#include <algorithm>

#include "core/InfluenceEye.h"

namespace bw {
namespace core {
using namespace std;

InfluenceEye::InfluenceEye()
    : InfluenceEye(wp::Vector2::ZERO, 0.0f, 360.0f) {
}

InfluenceEye::InfluenceEye(wp::Vector2 const& originOffset, float angleOffset, float arcLength)
    : mOriginOffset(originOffset), mAngleOffset(angleOffset), mArcLength(clamp(arcLength, 0.0f, 360.0f)) {
}

InfluenceEye::InfluenceEye(InfluenceEye const& other) {
  copyFrom(other);
}

InfluenceEye& InfluenceEye::operator=(InfluenceEye const& other) {
  copyFrom(other);
  return *this;
}

void InfluenceEye::copyFrom(InfluenceEye const& other) {
  mOriginOffset = other.mOriginOffset;
  mAngleOffset = other.mAngleOffset;
}

bool InfluenceEye::childrenModified() const {
  return false;
}

void InfluenceEye::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("influenceEye");
  {
    serializer->writeVector2("originOffset", mOriginOffset);
    serializer->writeFloat("angleOffset", mAngleOffset);
    serializer->writeFloat("arcLength", mArcLength);

    serializer->endMap();  // influenceEye
  }
}

bool InfluenceEye::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  wp::Vector2 originOffset;
  float angleOffset, arcLength;

  try {
    serializer->beginMap("influenceEye");
    {
      originOffset = serializer->readVector2("originOffset");
      angleOffset = serializer->readFloat("angleOffset");
      arcLength = serializer->readFloat("arcLength");

      serializer->endMap();  // influenceEye
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mOriginOffset = originOffset;
  mAngleOffset = angleOffset;
  mArcLength = arcLength;

  return true;
}

void InfluenceEye::setOriginOffset(wp::Vector2 const& originOffset) {
  mOriginOffset = originOffset;
}

wp::Vector2 const& InfluenceEye::getOriginOffset() const {
  return mOriginOffset;
}

void InfluenceEye::setAngleOffset(float angleOffset) {
  mAngleOffset = angleOffset;
}

float InfluenceEye::getAngleOffset() const {
  return mAngleOffset;
}

void InfluenceEye::setArcLength(float arcLength) {
  mArcLength = clamp(arcLength, 0.0f, 360.0f);
}

float InfluenceEye::getArcLength() const {
  return mArcLength;
}

bool InfluenceEye::inArc(wp::Vector2 const& position) const {
  // Do we pre-transform position relative to the owning Primitive's
  // global position, or pass it in?  Or do we do this in relative space,
  // ie assume the eye is at (0,0)?
  return false;
}

}  // namespace core
}  // namespace bw
