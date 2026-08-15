#include "core/CircleSegmentPolygon.h"

namespace bw {
namespace core {

using namespace std;

CircleSegmentPolygon::CircleSegmentPolygon()
    : RegularPolygon(), mArcLength(90.0f), mResolution(1.0f) {
}

CircleSegmentPolygon::CircleSegmentPolygon(Operation operation, FillRule fillType, float arcLength, float resolution)
    : RegularPolygon(operation, fillType, (uint32_t)(resolution * BaseResolution)), mArcLength(arcLength), mResolution(resolution) {
  generateVertices();
}

CircleSegmentPolygon::CircleSegmentPolygon(CircleSegmentPolygon const& other) {
  copyFrom(other);
}

CircleSegmentPolygon& CircleSegmentPolygon::operator=(CircleSegmentPolygon const& other) {
  copyFrom(other);
  return *this;
}

void CircleSegmentPolygon::copyFrom(CircleSegmentPolygon const& other) {
  RegularPolygon::copyFrom(other);

  mArcLength = other.mArcLength;
  mResolution = other.mResolution;
}

void CircleSegmentPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  RegularPolygon::serializeImpl(serializer, workData);

  serializer->beginMap("circleSegmentPolygon");
  {
    serializer->writeFloat("arcLength", mArcLength);
    serializer->writeFloat("resolution", mResolution);

    serializer->endMap();  // circleSegmentPolygon
  }
}

bool CircleSegmentPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!RegularPolygon::deserializeImpl(serializer, workData)) {
    return false;
  }

  float arcLength, resolution;

  try {
    serializer->beginMap("circleSegmentPolygon");
    {
      arcLength = serializer->readFloat("arcLength");
      resolution = serializer->readFloat("resolution");

      serializer->endMap();  // circleSegmentPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mArcLength = arcLength;
  mResolution = resolution;

  return true;
}

Primitive* CircleSegmentPolygon::copy() const {
  return new CircleSegmentPolygon(*this);
}

string CircleSegmentPolygon::getType() const {
  return "CircleSegment";
}

void CircleSegmentPolygon::setArcLength(float arcLength) {
  mArcLength = clamp(arcLength, 0.01f, 360.0f);
  generateVertices();
}

float CircleSegmentPolygon::getArcLength() const {
  return mArcLength;
}

void CircleSegmentPolygon::setNumSides(uint32_t numSides) {
  mResolution = (numSides * 360.0f) / (mArcLength * BaseResolution);
  RegularPolygon::setNumSides(numSides);  // calls generateVertices()
}

void CircleSegmentPolygon::setResolution(float resolution) {
  mResolution = clamp(resolution, 0.0f, 1.0f);
  mNumSides = (uint32_t)(resolution * (mArcLength / 360.0f) * BaseResolution);
  generateVertices();
}

float CircleSegmentPolygon::getResolution() const {
  return mResolution;
}

vector<ComplexPolygon> CircleSegmentPolygon::generateVerticesImpl() {
  assert(mNumSides >= 3 && "Too few sides.");

  if (mArcLength == 360.0f) {
    return RegularPolygon::generateVerticesImpl();
  } else {
    ClosedPolygon vertices(mNumSides + 2);

    vertices[0] = {wp::Vector2::ZERO, 0};

    float offset = -mArcLength * 0.5f;
    for (uint32_t i = 0; i <= mNumSides; ++i) {
      float d = i / (float)mNumSides;
      float angle = offset + mArcLength * d;

      vertices[i + 1] = {
          wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle),
          0};
    }

    return {{vertices}};
  }
}

}  // namespace core
}  // namespace bw