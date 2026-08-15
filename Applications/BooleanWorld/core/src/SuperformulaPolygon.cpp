#include <cmath>

#include <willpower/common/Globals.h>

#include "core/SuperformulaPolygon.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {

using namespace std;

SuperformulaPolygon::SuperformulaPolygon()
    : Primitive(), mResolution(1.0f), mValues() {
}

SuperformulaPolygon::SuperformulaPolygon(Operation operation, FillRule fillType, float resolution, float values[6])
    : Primitive(operation, fillType), mResolution(resolution) {
  for (int i = 0; i < 6; ++i) {
    mValues[i] = values[i];
  }

  generateVertices();
}

SuperformulaPolygon::SuperformulaPolygon(SuperformulaPolygon const& other) {
  copyFrom(other);
}

SuperformulaPolygon& SuperformulaPolygon::operator=(SuperformulaPolygon const& other) {
  copyFrom(other);
  return *this;
}

void SuperformulaPolygon::copyFrom(SuperformulaPolygon const& other) {
  Primitive::copyFrom(other);

  for (int i = 0; i < 6; ++i) {
    mValues[i] = other.mValues[i];
  }
}

void SuperformulaPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("superformulaPolygon");
  {
    serializer->writeFloat("resolution", mResolution);

    serializer->beginArray("values", false);
    {
      serializer->writeFloat("a", mValues[0]);
      serializer->writeFloat("b", mValues[1]);
      serializer->writeFloat("m", mValues[2]);
      serializer->writeFloat("n1", mValues[3]);
      serializer->writeFloat("n2", mValues[4]);
      serializer->writeFloat("n3", mValues[5]);

      serializer->endArray();
    }

    serializer->endMap();  // superformulaPolygon
  }
}

bool SuperformulaPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  float resolution;
  float values[6];

  try {
    serializer->beginMap("superformulaPolygon");
    {
      resolution = serializer->readFloat("resolution");

      serializer->beginArray("values");
      {
        int i = 0;
        while (serializer->nextArrayItem()) {
          if (i >= 6) {
            throw SerializationException("More than 6 control values found for Superformula primitive.");
          }

          values[i++] = serializer->readFloat();
        }

        serializer->endArray();
      }

      serializer->endMap();  // superformulaPolygon
    }

  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mResolution = resolution;

  for (int i = 0; i < 6; ++i) {
    mValues[i] = values[i];
  }

  return true;
}

float SuperformulaPolygon::getRadius() const {
  throw 1.0f;
}

float SuperformulaPolygon::r(float theta) const {
  return (float)pow(
      pow(abs(cos(mValues[2] * theta / 4.0) / mValues[0]), mValues[4]) +
          pow(abs(sin(mValues[2] * theta / 4.0) / mValues[1]), mValues[5]),
      -1.0 / mValues[3]);
}

wp::Vector2 SuperformulaPolygon::calculate(float theta) const {
  float rad = r(theta);

  float x = rad * cosf(theta);
  float y = rad * sinf(theta);

  return {x, y};
}

Primitive* SuperformulaPolygon::copy() const {
  return new SuperformulaPolygon(*this);
}

string SuperformulaPolygon::getType() const {
  return "Superformula";
}

void SuperformulaPolygon::setResolution(float resolution) {
  mResolution = resolution;
  generateVertices();
}

float SuperformulaPolygon::getResolution() const {
  return mResolution;
}

void SuperformulaPolygon::setValue(uint32_t index, float value) {
  mValues[index] = value;
  generateVertices();
}

float SuperformulaPolygon::getValue(uint32_t index) const {
  return mValues[index];
}

vector<ComplexPolygon> SuperformulaPolygon::generateVerticesImpl() {
  ClosedPolygon vertices;

  float inc = 1.0f / (BaseResolution * mResolution);

  for (float a = 0.0f; a < WP_TWOPI; a += inc) {
    vertices.push_back({calculate(a), 0});
  }

  return {{vertices}};
}

}  // namespace core
}  // namespace bw