#include <cmath>
#include <format>

#include "core/CoreException.h"
#include "core/Defines.h"
#include "core/TorusPolygon.h"

namespace bw {
namespace core {

using namespace std;

namespace {

float validateThickness(float thickness) {
  if (!isfinite(thickness) || thickness < 0.01f || thickness > 0.99f) {
    throw CoreException("Torus thickness must be finite and in [0.01, 0.99]");
  }
  return thickness;
}

uint32_t sideCountForResolution(float resolution, uint32_t baseResolution) {
  if (!isfinite(resolution) || resolution < 3.0f / baseResolution || resolution > 1.0f) {
    throw CoreException(format(
        "Torus resolution must be finite and in [{}, 1]", 3.0f / baseResolution));
  }

  auto const numSides = static_cast<uint32_t>(resolution * baseResolution);
  if (numSides < 3 || numSides > BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX) {
    throw CoreException(format(
        "Torus resolution must produce between 3 and {} vertices per contour",
        BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX));
  }
  return numSides;
}

}  // namespace

TorusPolygon::TorusPolygon()
    : Primitive(), mThickness(0.5f), mResolution(1.0f), mNumSides(BaseResolution) {
}

TorusPolygon::TorusPolygon(Operation operation, FillRule fillType, float thickness, float resolution)
    : Primitive(operation, fillType), mThickness(validateThickness(thickness)), mResolution(resolution), mNumSides(sideCountForResolution(resolution, BaseResolution)) {
  generateVertices();
}

TorusPolygon::TorusPolygon(TorusPolygon const& other) {
  copyFrom(other);
}

TorusPolygon& TorusPolygon::operator=(TorusPolygon const& other) {
  copyFrom(other);
  return *this;
}

void TorusPolygon::copyFrom(TorusPolygon const& other) {
  Primitive::copyFrom(other);

  mThickness = other.mThickness;
  mResolution = other.mResolution;
  mNumSides = other.mNumSides;
}

Primitive* TorusPolygon::copy() const {
  return new TorusPolygon(*this);
}

string TorusPolygon::getType() const {
  return "Torus";
}

void TorusPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("torusPolygon");
  {
    serializer->writeFloat("thickness", mThickness);
    serializer->writeFloat("resolution", mResolution);
    serializer->writeUint32("numSides", mNumSides);

    serializer->endMap();  // torusPolygon
  }
}

bool TorusPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  float thickness, resolution;
  uint32_t numSides;

  try {
    serializer->beginMap("torusPolygon");
    {
      thickness = validateThickness(serializer->readFloat("thickness"));
      resolution = serializer->readFloat("resolution");
      numSides = serializer->readUint32("numSides");
      auto const expectedNumSides = sideCountForResolution(resolution, BaseResolution);
      if (numSides != expectedNumSides) {
        throw CoreException("Torus resolution does not match its side count");
      }

      serializer->endMap();  // torusPolygon
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mThickness = thickness;
  mResolution = resolution;
  mNumSides = numSides;

  return true;
}

vector<ComplexPolygon> TorusPolygon::generateVerticesImpl() {
  validateThickness(mThickness);
  auto const expectedNumSides = sideCountForResolution(mResolution, BaseResolution);
  if (mNumSides != expectedNumSides) {
    throw CoreException("Torus resolution does not match its side count");
  }

  ClosedPolygon outerVertices(mNumSides), innerVertices(mNumSides);

  for (uint32_t i = 0; i < mNumSides; ++i) {
    float angle = 360.0f * i / (float)mNumSides;
    outerVertices[i] = {
        wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle)};

    innerVertices[mNumSides - i - 1] = {outerVertices[i].p * (1.0f - mThickness)};
  }

  return {{outerVertices, innerVertices}};
}

float TorusPolygon::getRadius() const {
  return 1.0f;
}

void TorusPolygon::setThickness(float thickness) {
  auto const validatedThickness = validateThickness(thickness);
  mThickness = validatedThickness;
  generateVertices();
}

float TorusPolygon::getThickness() const {
  return mThickness;
}

void TorusPolygon::setResolution(float resolution) {
  auto const numSides = sideCountForResolution(resolution, BaseResolution);
  mResolution = resolution;
  mNumSides = numSides;
  generateVertices();
}

float TorusPolygon::getResolution() const {
  return mResolution;
}

void TorusPolygon::setNumSides(uint32_t numSides) {
  auto const resolution = numSides / static_cast<float>(BaseResolution);
  sideCountForResolution(resolution, BaseResolution);
  mNumSides = numSides;
  mResolution = resolution;
  generateVertices();
}

uint32_t TorusPolygon::getNumSides() const {
  return mNumSides;
}

}  // namespace core
}  // namespace bw