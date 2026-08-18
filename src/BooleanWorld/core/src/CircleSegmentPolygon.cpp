#include <cmath>
#include <format>

#include "core/CircleSegmentPolygon.h"
#include "core/CoreException.h"

namespace bw {
namespace core {

using namespace std;

namespace {

float validateArcLength(float arcLength) {
  if (!isfinite(arcLength) || arcLength < 0.01f || arcLength > 360.0f) {
    throw CoreException("Circle segment arc length must be finite and in [0.01, 360]");
  }
  return arcLength;
}

uint32_t sideCountForParameters(float arcLength, float resolution, uint32_t baseResolution) {
  validateArcLength(arcLength);
  if (!isfinite(resolution) || resolution < 0.0f || resolution > 1.0f) {
    throw CoreException("Circle segment resolution must be finite and in [0, 1]");
  }

  auto const numSides = static_cast<uint32_t>(resolution * baseResolution);
  if (numSides < 3) {
    throw CoreException(format(
        "Circle segment resolution must produce at least 3 arc boundary vertices (minimum is {})",
        3.0f / baseResolution));
  }
  return numSides;
}

}  // namespace

CircleSegmentPolygon::CircleSegmentPolygon()
    : RegularPolygon(), mArcLength(90.0f), mResolution(1.0f) {
}

CircleSegmentPolygon::CircleSegmentPolygon(Operation operation, FillRule fillType, float arcLength, float resolution)
    : RegularPolygon(operation, fillType, sideCountForParameters(arcLength, resolution, BaseResolution)), mArcLength(arcLength), mResolution(resolution) {
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
      auto const expectedNumSides = sideCountForParameters(arcLength, resolution, BaseResolution);
      if (mNumSides != expectedNumSides) {
        throw CoreException("Circle segment resolution does not match its regular polygon side count");
      }

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
  auto const validatedArcLength = validateArcLength(arcLength);
  mArcLength = validatedArcLength;
  generateVertices();
}

float CircleSegmentPolygon::getArcLength() const {
  return mArcLength;
}

void CircleSegmentPolygon::setNumSides(uint32_t numSides) {
  auto const resolution = numSides / static_cast<float>(BaseResolution);
  sideCountForParameters(mArcLength, resolution, BaseResolution);
  mResolution = resolution;
  RegularPolygon::setNumSides(numSides);  // calls generateVertices()
}

void CircleSegmentPolygon::setResolution(float resolution) {
  auto const numSides = sideCountForParameters(mArcLength, resolution, BaseResolution);
  mResolution = resolution;
  mNumSides = numSides;
  generateVertices();
}

float CircleSegmentPolygon::getResolution() const {
  return mResolution;
}

vector<ComplexPolygon> CircleSegmentPolygon::generateVerticesImpl() {
  sideCountForParameters(mArcLength, mResolution, BaseResolution);

  if (mArcLength == 360.0f) {
    return RegularPolygon::generateVerticesImpl();
  } else {
    ClosedPolygon vertices(mNumSides + 2);

    vertices[0] = {wp::Vector2::ZERO};

    float offset = -mArcLength * 0.5f;
    for (uint32_t i = 0; i <= mNumSides; ++i) {
      float d = i / (float)mNumSides;
      float angle = offset + mArcLength * d;

      vertices[i + 1] = {
          wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle)};
    }

    return {{vertices}};
  }
}

}  // namespace core
}  // namespace bw