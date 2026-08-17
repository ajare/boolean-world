#include <format>

#include "core/MeshPrimitive.h"

namespace bw {
namespace core {

using namespace std;

MeshPrimitive::MeshPrimitive()
    : Primitive() {
}

MeshPrimitive::MeshPrimitive(Operation operation, FillRule fillType, vector<ComplexPolygon> const& polygons)
    : Primitive(operation, fillType, polygons) {
  generateVertices();
}

MeshPrimitive::MeshPrimitive(MeshPrimitive const& other) {
  copyFrom(other);
}

MeshPrimitive& MeshPrimitive::operator=(MeshPrimitive const& other) {
  copyFrom(other);
  return *this;
}

MeshPrimitive* MeshPrimitive::fromComplexPolygons(
    Operation operation,
    FillRule fillType,
    vector<ComplexPolygon> complexPolygons) {
  auto bounds = calculatePolygonBounds(complexPolygons);

  // Recentre and rescale vertices so that they are in unit space around the local origin
  auto const& pCentre = bounds.getCentre();
  auto halfSize = bounds.getHalfSize();
  auto scale = std::max(halfSize.x, halfSize.y);

  for (auto& complexPolygon : complexPolygons) {
    for (auto& polygon : complexPolygon) {
      auto numVertices = (uint32_t)polygon.size();
      for (uint32_t i = 0; i < numVertices; ++i) {
        polygon[i].p -= pCentre;
        polygon[i].p /= scale;
      }
    }
  }

  // Create new primitive
  auto p = new MeshPrimitive(operation, fillType, complexPolygons);

  p->setSize(scale * 2, scale * 2);
  p->setPosition(pCentre);

  // Set the scale and angle default values here, so that if we toggle them off/on, we don't lose the settings we
  // specified at creation.  Orbit angle and distance are not specified at creation so we can hardcode those defaults.
  {
    auto mutation = p->mutate();
    mutation.animation(VertexTransformer::Key::Scale).setDefaultStructure({{0.0f, 1.0f}, {1.0f, 1.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::Angle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitAngle).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
    mutation.animation(VertexTransformer::Key::OrbitDistance).setDefaultStructure({{0.0f, 0.0f}, {1.0f, 0.0f}}, {{bw::core::Easing::Linear}}, true);
  }

  return p;
}

void MeshPrimitive::copyFrom(MeshPrimitive const& other) {
  Primitive::copyFrom(other);
}

Primitive* MeshPrimitive::copy() const {
  return new MeshPrimitive(*this);
}

string MeshPrimitive::getType() const {
  return "Mesh";
}

string MeshPrimitive::getName() const {
  if (getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return "Ghost";
  } else {
    return "Mesh";
  }
}

void MeshPrimitive::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  Primitive::serializeImpl(serializer, workData);

  serializer->beginMap("meshPrimitive");
  {
    serializer->endMap();  // meshPrimitive
  }
}

bool MeshPrimitive::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  if (!Primitive::deserializeImpl(serializer, workData)) {
    return false;
  }

  try {
    serializer->beginMap("meshPrimitive");
    {
      serializer->endMap();  // meshPrimitive
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  return true;
}

vector<ComplexPolygon> MeshPrimitive::generateVerticesImpl() {
  return mPolygons;
}

float MeshPrimitive::getRadius() const {
  return 1.0f;
}

}  // namespace core
}  // namespace bw