#include <algorithm>
#include <array>
#include <cmath>

#include <willpower/common/Globals.h>

#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/SuperformulaPolygon.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr size_t ControlValueCount = 6;
constexpr uint32_t SamplingBaseResolution = 64;
constexpr float MaximumResolution =
    BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX / static_cast<float>(SamplingBaseResolution);

float validateResolution(float resolution) {
  if (!isfinite(resolution) || resolution <= 0.0f || resolution > MaximumResolution) {
    throw CoreException(
        "Superformula resolution must be finite, positive, and within the contour vertex limit");
  }
  return resolution;
}

uint32_t sampleCountForResolution(float resolution) {
  return max(3u, static_cast<uint32_t>(resolution * SamplingBaseResolution));
}

void validateControlValues(array<float, ControlValueCount> const& values) {
  for (auto value : values) {
    if (!isfinite(value)) {
      throw CoreException("Superformula control values must be finite");
    }
  }

  if (values[0] <= 0.0f || values[1] <= 0.0f) {
    throw CoreException("Superformula denominators a and b must be positive");
  }
  if (values[3] <= 0.0f || values[4] <= 0.0f || values[5] <= 0.0f) {
    throw CoreException("Superformula exponents n1, n2, and n3 must be positive");
  }
}

array<float, ControlValueCount> copyControlValues(float const* values) {
  array<float, ControlValueCount> result;
  copy_n(values, ControlValueCount, result.begin());
  return result;
}

float formulaRadius(float theta, array<float, ControlValueCount> const& values) {
  return static_cast<float>(pow(
      pow(abs(cos(values[2] * theta / 4.0) / values[0]), values[4]) +
          pow(abs(sin(values[2] * theta / 4.0) / values[1]), values[5]),
      -1.0 / values[3]));
}

vector<ComplexPolygon> generateSuperformulaVertices(
    float resolution, array<float, ControlValueCount> const& values) {
  validateResolution(resolution);
  validateControlValues(values);

  auto const sampleCount = sampleCountForResolution(resolution);
  ClosedPolygon vertices;
  vertices.reserve(sampleCount);
  for (uint32_t sample = 0; sample < sampleCount; ++sample) {
    float const angle =
        static_cast<float>(WP_TWOPI) * sample / static_cast<float>(sampleCount);
    float const radius = formulaRadius(angle, values);
    wp::Vector2 const vertex{radius * cosf(angle), radius * sinf(angle)};
    if (!isfinite(vertex.x) || !isfinite(vertex.y)) {
      throw CoreException("Superformula generated a non-finite vertex");
    }
    vertices.push_back({vertex});
  }
  return {{vertices}};
}

}  // namespace

SuperformulaPolygon::SuperformulaPolygon()
    : Primitive(), mResolution(1.0f), mValues() {
}

SuperformulaPolygon::SuperformulaPolygon(Operation operation, FillRule fillType, float resolution, float values[6])
    : Primitive(operation, fillType), mResolution(validateResolution(resolution)) {
  auto const validatedValues = copyControlValues(values);
  validateControlValues(validatedValues);
  std::copy(validatedValues.begin(), validatedValues.end(), mValues);
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

  mResolution = other.mResolution;
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
  array<float, ControlValueCount> values{};

  try {
    serializer->beginMap("superformulaPolygon");
    {
      resolution = validateResolution(serializer->readFloat("resolution"));

      serializer->beginArray("values");
      {
        size_t count = 0;
        while (serializer->nextArrayItem()) {
          if (count >= ControlValueCount) {
            throw SerializationException("Exactly 6 control values are required for Superformula primitive");
          }
          values[count++] = serializer->readFloat();
        }
        if (count != ControlValueCount) {
          throw SerializationException("Exactly 6 control values are required for Superformula primitive");
        }

        serializer->endArray();
      }

      validateControlValues(values);
      generateSuperformulaVertices(resolution, values);

      serializer->endMap();  // superformulaPolygon
    }

  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mResolution = resolution;
  std::copy(values.begin(), values.end(), mValues);

  return true;
}

float SuperformulaPolygon::getRadius() const {
  float radius = 0.0f;

  for (auto const& complexPolygon : mPolygons) {
    for (auto const& contour : complexPolygon) {
      for (auto const& vertex : contour) {
        radius = max(radius, sqrt(vertex.p.x * vertex.p.x + vertex.p.y * vertex.p.y));
      }
    }
  }

  return radius;
}

float SuperformulaPolygon::r(float theta) const {
  return formulaRadius(theta, copyControlValues(mValues));
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
  auto const validatedResolution = validateResolution(resolution);
  auto const vertices = generateSuperformulaVertices(
      validatedResolution, copyControlValues(mValues));
  mResolution = validatedResolution;
  setVertices(vertices);
}

float SuperformulaPolygon::getResolution() const {
  return mResolution;
}

void SuperformulaPolygon::setValue(uint32_t index, float value) {
  if (index >= ControlValueCount) {
    throw CoreException("Superformula control value index is out of range");
  }

  auto values = copyControlValues(mValues);
  values[index] = value;
  auto const vertices = generateSuperformulaVertices(mResolution, values);
  std::copy(values.begin(), values.end(), mValues);
  setVertices(vertices);
}

float SuperformulaPolygon::getValue(uint32_t index) const {
  return mValues[index];
}

vector<ComplexPolygon> SuperformulaPolygon::generateVerticesImpl() {
  return generateSuperformulaVertices(mResolution, copyControlValues(mValues));
}

}  // namespace core
}  // namespace bw