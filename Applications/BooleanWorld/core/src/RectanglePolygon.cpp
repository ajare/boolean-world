#include "core/RectanglePolygon.h"

namespace bw {
namespace core {

using namespace std;

RectanglePolygon::RectanglePolygon()
    : Primitive(), mXyRatio(1.0f) {
}

RectanglePolygon::RectanglePolygon(Operation operation, FillRule fillType, float xyRatio)
    : Primitive(operation, fillType), mXyRatio(xyRatio) {
  generateVertices();
}

RectanglePolygon::RectanglePolygon(RectanglePolygon const& other) {
  copyFrom(other);
}

RectanglePolygon& RectanglePolygon::operator=(RectanglePolygon const& other) {
  copyFrom(other);
  return *this;
}

void RectanglePolygon::copyFrom(RectanglePolygon const& other) {
  Primitive::copyFrom(other);

  mXyRatio = other.mXyRatio;
}

Primitive* RectanglePolygon::copy() const {
  return new RectanglePolygon(*this);
}

string RectanglePolygon::getType() const {
  return "Rectangle";
}

void RectanglePolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("rectanglePolygon");
  {
    serializer->writeFloat("xyRatio", mXyRatio);

    serializer->endMap();  // rectanglePolygon
  }
}

bool RectanglePolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  float xyRatio;

  try {
    serializer->beginMap("rectanglePolygon");
    {
      xyRatio = serializer->readFloat("xyRatio");

      serializer->endMap();  // rectanglePolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mXyRatio = xyRatio;

  return true;
}

vector<ComplexPolygon> RectanglePolygon::generateVerticesImpl() {
  float i1 = 1.0f / mXyRatio;

  return {{{{wp::Vector2(1.0f, i1), 0},
            {wp::Vector2(-1.0f, i1), 0},
            {wp::Vector2(-1.0f, -i1), 0},
            {wp::Vector2(1.0f, -i1), 0}}}};
}

float RectanglePolygon::getRadius() const {
  auto ir = 1.0f / getXyRatio();

  return sqrtf(ir * ir + 1.0f);
}

void RectanglePolygon::setXyRatio(float xyRatio) {
  mXyRatio = xyRatio;
  generateVertices();
}

float RectanglePolygon::getXyRatio() const {
  return mXyRatio;
}

}  // namespace core
}  // namespace bw