#include <format>

#include "core/RegularPolygon.h"

namespace bw {
namespace core {

using namespace std;

RegularPolygon::RegularPolygon()
    : Primitive(), mNumSides(0) {
}

RegularPolygon::RegularPolygon(Operation operation, FillRule fillType, uint32_t numSides)
    : Primitive(operation, fillType), mNumSides(numSides) {
  generateVertices();
}

RegularPolygon::RegularPolygon(RegularPolygon const& other) {
  copyFrom(other);
}

RegularPolygon& RegularPolygon::operator=(RegularPolygon const& other) {
  copyFrom(other);
  return *this;
}

void RegularPolygon::copyFrom(RegularPolygon const& other) {
  Primitive::copyFrom(other);
  mNumSides = other.mNumSides;
}

Primitive* RegularPolygon::copy() const {
  return new RegularPolygon(*this);
}

string RegularPolygon::getType() const {
  return "Regular";
}

string RegularPolygon::getName() const {
  if (getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return "Ghost";
  } else {
    return format("Regular {}-Gon", mNumSides);
  }
}

void RegularPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("regularPolygon");
  {
    serializer->writeUint32("numSides", mNumSides);

    serializer->endMap();  // regularPolygon
  }
}

bool RegularPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  uint32_t numSides;

  try {
    serializer->beginMap("regularPolygon");
    {
      numSides = serializer->readUint32("numSides");

      serializer->endMap();  // regularPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mNumSides = numSides;
  return true;
}

vector<ComplexPolygon> RegularPolygon::generateVerticesImpl() {
  assert(mNumSides >= 3 && "Too few sides.");

  ClosedPolygon vertices(mNumSides);

  for (uint32_t i = 0; i < mNumSides; ++i) {
    float angle = 360.0f * i / (float)mNumSides;
    vertices[i] = {
        wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle)};
  }

  return {{vertices}};
}

float RegularPolygon::getRadius() const {
  return 1.0f;
}

void RegularPolygon::setNumSides(uint32_t numSides) {
  mNumSides = numSides;
  generateVertices();
}

uint32_t RegularPolygon::getNumSides() const {
  return mNumSides;
}

}  // namespace core
}  // namespace bw