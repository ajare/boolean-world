#include "core/TorusSegmentPolygon.h"

namespace bw {
namespace core {

using namespace std;

TorusSegmentPolygon::TorusSegmentPolygon()
    : Primitive(), mThickness(0.5f), mArcLength(90.0f), mResolution(1.0f), mNumSides(BaseResolution) {
}

TorusSegmentPolygon::TorusSegmentPolygon(Operation operation, FillRule fillType, float thickness, float arcLength, float resolution)
    : Primitive(operation, fillType), mThickness(thickness), mArcLength(arcLength), mResolution(resolution), mNumSides((uint32_t)(resolution * BaseResolution)) {
  generateVertices();
}

TorusSegmentPolygon::TorusSegmentPolygon(TorusSegmentPolygon const& other) {
  copyFrom(other);
}

TorusSegmentPolygon& TorusSegmentPolygon::operator=(TorusSegmentPolygon const& other) {
  copyFrom(other);
  return *this;
}

void TorusSegmentPolygon::copyFrom(TorusSegmentPolygon const& other) {
  Primitive::copyFrom(other);

  mThickness = other.mThickness;
  mArcLength = other.mArcLength;
  mResolution = other.mResolution;
  mNumSides = other.mNumSides;
}

Primitive* TorusSegmentPolygon::copy() const {
  return new TorusSegmentPolygon(*this);
}

string TorusSegmentPolygon::getType() const {
  return "TorusSegment";
}

void TorusSegmentPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("torusPolygon");
  {
    serializer->writeFloat("thickness", mThickness);
    serializer->writeFloat("arcLength", mArcLength);
    serializer->writeFloat("resolution", mResolution);
    serializer->writeUint32("numSides", mNumSides);

    serializer->endMap();  // torusPolygon
  }
}

bool TorusSegmentPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  float thickness, arcLength, resolution;
  uint32_t numSides;

  try {
    serializer->beginMap("torusPolygon");
    {
      thickness = serializer->readFloat("thickness");
      arcLength = serializer->readFloat("arcLength");
      resolution = serializer->readFloat("resolution");
      numSides = serializer->readUint32("numSides");

      serializer->endMap();  // torusPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mThickness = thickness;
  mArcLength = arcLength;
  mResolution = resolution;
  mNumSides = numSides;

  return true;
}

vector<ComplexPolygon> TorusSegmentPolygon::generateVerticesImpl() {
  assert(mNumSides >= 3 && "Too few sides.");

  if (mArcLength == 360.0f) {
    ClosedPolygon outerVertices(mNumSides), innerVertices(mNumSides);

    for (uint32_t i = 0; i < mNumSides; ++i) {
      float angle = 360.0f * i / (float)mNumSides;
      outerVertices[i] = {
          wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle)};

      innerVertices[mNumSides - i - 1] = {outerVertices[i].p * (1.0f - mThickness)};
    }

    return {{outerVertices, innerVertices}};
  } else {
    auto nv = (mNumSides + 1) * 2;
    ClosedPolygon vertices(nv);

    float offset = -mArcLength * 0.5f;

    for (uint32_t i = 0; i <= mNumSides; ++i) {
      float d = i / (float)mNumSides;
      float angle = offset + mArcLength * d;

      vertices[i] = {
          wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle)};

      vertices[nv - i - 1] = {
          vertices[i].p * mThickness};
    }

    return {{vertices}};
  }
}

float TorusSegmentPolygon::getRadius() const {
  return 1.0f;
}

void TorusSegmentPolygon::setThickness(float thickness) {
  mThickness = thickness;
  generateVertices();
}

float TorusSegmentPolygon::getThickness() const {
  return mThickness;
}

void TorusSegmentPolygon::setArcLength(float arcLength) {
  mArcLength = clamp(arcLength, 0.01f, 360.0f);
  generateVertices();
}

float TorusSegmentPolygon::getArcLength() const {
  return mArcLength;
}

void TorusSegmentPolygon::setResolution(float resolution) {
  mResolution = resolution;
  mNumSides = (uint32_t)(resolution * BaseResolution);
  generateVertices();
}

float TorusSegmentPolygon::getResolution() const {
  return mResolution;
}

void TorusSegmentPolygon::setNumSides(uint32_t numSides) {
  mNumSides = numSides;
  mResolution = mNumSides / (float)BaseResolution;
  generateVertices();
}

uint32_t TorusSegmentPolygon::getNumSides() const {
  return mNumSides;
}

}  // namespace core
}  // namespace bw