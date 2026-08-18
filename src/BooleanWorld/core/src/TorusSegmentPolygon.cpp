#include <cmath>
#include <format>

#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/TorusSegmentPolygon.h"

namespace bw {
namespace core {

using namespace std;

namespace {

float validateThickness(float thickness) {
  if (!isfinite(thickness) || thickness < 0.01f || thickness > 0.99f) {
    throw CoreException("Torus segment thickness must be finite and in [0.01, 0.99]");
  }
  return thickness;
}

float validateArcLength(float arcLength) {
  if (!isfinite(arcLength) || arcLength < 0.01f || arcLength > 360.0f) {
    throw CoreException("Torus segment arc length must be finite and in [0.01, 360]");
  }
  return arcLength;
}

uint32_t sideCountForParameters(float arcLength, float resolution, uint32_t baseResolution) {
  validateArcLength(arcLength);
  if (!isfinite(resolution) || resolution < 3.0f / baseResolution || resolution > 1.0f) {
    throw CoreException(format(
        "Torus segment resolution must be finite and in [{}, 1]",
        3.0f / baseResolution));
  }

  auto const numSides = static_cast<uint32_t>(resolution * baseResolution);
  auto const contourVertexCount =
      arcLength == 360.0f ? numSides : static_cast<uint64_t>(numSides + 1) * 2;
  if (numSides < 3 || contourVertexCount > BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
    throw CoreException(format(
        "Torus segment parameters must produce at least 3 arc vertices and no more than {} vertices per contour",
        BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX));
  }
  return numSides;
}

}  // namespace

TorusSegmentPolygon::TorusSegmentPolygon()
    : Primitive(), mThickness(0.5f), mArcLength(90.0f), mResolution(1.0f), mNumSides(BaseResolution) {
}

TorusSegmentPolygon::TorusSegmentPolygon(Operation operation, FillRule fillType, float thickness, float arcLength, float resolution)
    : Primitive(operation, fillType), mThickness(validateThickness(thickness)), mArcLength(validateArcLength(arcLength)), mResolution(resolution), mNumSides(sideCountForParameters(arcLength, resolution, BaseResolution)) {
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
      thickness = validateThickness(serializer->readFloat("thickness"));
      arcLength = validateArcLength(serializer->readFloat("arcLength"));
      resolution = serializer->readFloat("resolution");
      numSides = serializer->readUint32("numSides");
      auto const expectedNumSides = sideCountForParameters(arcLength, resolution, BaseResolution);
      if (numSides != expectedNumSides) {
        throw CoreException("Torus segment resolution does not match its side count");
      }

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
  validateThickness(mThickness);
  auto const expectedNumSides = sideCountForParameters(mArcLength, mResolution, BaseResolution);
  if (mNumSides != expectedNumSides) {
    throw CoreException("Torus segment resolution does not match its side count");
  }

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
          vertices[i].p * (1.0f - mThickness)};
    }

    return {{vertices}};
  }
}

float TorusSegmentPolygon::getRadius() const {
  return 1.0f;
}

void TorusSegmentPolygon::setThickness(float thickness) {
  auto const validatedThickness = validateThickness(thickness);
  mThickness = validatedThickness;
  generateVertices();
}

float TorusSegmentPolygon::getThickness() const {
  return mThickness;
}

void TorusSegmentPolygon::setArcLength(float arcLength) {
  auto const validatedArcLength = validateArcLength(arcLength);
  sideCountForParameters(validatedArcLength, mResolution, BaseResolution);
  mArcLength = validatedArcLength;
  generateVertices();
}

float TorusSegmentPolygon::getArcLength() const {
  return mArcLength;
}

void TorusSegmentPolygon::setResolution(float resolution) {
  auto const numSides = sideCountForParameters(mArcLength, resolution, BaseResolution);
  mResolution = resolution;
  mNumSides = numSides;
  generateVertices();
}

float TorusSegmentPolygon::getResolution() const {
  return mResolution;
}

void TorusSegmentPolygon::setNumSides(uint32_t numSides) {
  auto const resolution = numSides / static_cast<float>(BaseResolution);
  sideCountForParameters(mArcLength, resolution, BaseResolution);
  mNumSides = numSides;
  mResolution = resolution;
  generateVertices();
}

uint32_t TorusSegmentPolygon::getNumSides() const {
  return mNumSides;
}

}  // namespace core
}  // namespace bw