#include <cmath>
#include <format>

#include "core/CirclePolygon.h"
#include "core/CoreException.h"

namespace bw {
namespace core {

using namespace std;

namespace {

uint32_t sideCountForResolution(float resolution, uint32_t baseResolution) {
  if (!isfinite(resolution) || resolution < 0.0f || resolution > 1.0f) {
    throw CoreException("Circle resolution must be finite and in [0, 1]");
  }

  auto const numSides = static_cast<uint32_t>(resolution * baseResolution);
  if (numSides < 3) {
    throw CoreException(format(
        "Circle resolution must produce at least 3 boundary vertices (minimum is {})",
        3.0f / baseResolution));
  }
  return numSides;
}

}  // namespace

CirclePolygon::CirclePolygon()
    : RegularPolygon(), mResolution(1.0f) {
  mNumSides = BaseResolution;
  generateVertices();
}

CirclePolygon::CirclePolygon(Operation operation, FillRule fillType, float resolution)
    : RegularPolygon(operation, fillType, sideCountForResolution(resolution, BaseResolution)), mResolution(resolution) {
  generateVertices();
}

CirclePolygon::CirclePolygon(CirclePolygon const& other) {
  copyFrom(other);
}

CirclePolygon& CirclePolygon::operator=(CirclePolygon const& other) {
  copyFrom(other);
  return *this;
}

void CirclePolygon::copyFrom(CirclePolygon const& other) {
  RegularPolygon::copyFrom(other);

  mResolution = other.mResolution;
}

void CirclePolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  RegularPolygon::serializeImpl(serializer, workData);

  serializer->beginMap("circlePolygon");
  {
    serializer->writeFloat("resolution", mResolution);

    serializer->endMap();  // circlePolygon
  }
}

bool CirclePolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!RegularPolygon::deserializeImpl(serializer, workData)) {
    return false;
  }

  float resolution;

  try {
    serializer->beginMap("circlePolygon");
    {
      resolution = serializer->readFloat("resolution");
      auto const expectedNumSides = sideCountForResolution(resolution, BaseResolution);
      if (mNumSides != expectedNumSides) {
        throw CoreException("Circle resolution does not match its regular polygon side count");
      }

      serializer->endMap();  // regularPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mResolution = resolution;

  return true;
}

Primitive* CirclePolygon::copy() const {
  return new CirclePolygon(*this);
}

string CirclePolygon::getType() const {
  return "Circle";
}

void CirclePolygon::setNumSides(uint32_t numSides) {
  auto const resolution = numSides / static_cast<float>(BaseResolution);
  sideCountForResolution(resolution, BaseResolution);
  mResolution = resolution;
  RegularPolygon::setNumSides(numSides);  // calls generateVertices()
}

void CirclePolygon::setResolution(float resolution) {
  auto const numSides = sideCountForResolution(resolution, BaseResolution);
  mResolution = resolution;
  mNumSides = numSides;
  generateVertices();
}

float CirclePolygon::getResolution() const {
  return mResolution;
}

}  // namespace core
}  // namespace bw