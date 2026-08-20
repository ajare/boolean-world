#pragma once

#include <memory>

#include <willpower/geometry/Mesh.h>

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/Vertex.h"

namespace bw {
namespace core {

class BW_API MeshPrimitive : public Primitive {
  friend class Primitive;  // Only Primitive::instantiate calls the default constructor (during deserialization)

protected:
  MeshPrimitive();

  void copyFrom(MeshPrimitive const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  std::vector<ComplexPolygon> generateVerticesImpl() override;

public:
  MeshPrimitive(Operation operation, FillRule fillType, std::vector<ComplexPolygon> const& polygons);

  MeshPrimitive(MeshPrimitive const& other);

  MeshPrimitive& operator=(MeshPrimitive const& other);

  static MeshPrimitive* fromComplexPolygons(
      Operation operation,
      FillRule fillType,
      std::vector<ComplexPolygon> polygons);

  // Builds the editing representation described by ADR-0016. Coordinates are
  // the Primitive's t=0 world-space rest pose; Ring 0 is each outer boundary.
  [[nodiscard]] std::unique_ptr<wp::geometry::Mesh> createGeometryProxy() const;

  // Commits a geometry proxy back through the inverse rest-pose transform.
  // This deliberately replaces a general Ring setter: callers can only write
  // topology represented by the validated editing type.
  void updateFromGeometryProxy(wp::geometry::Mesh const& mesh);

  Primitive* copy() const override;

  std::string getType() const override;

  std::string getName() const override;

  float getRadius() const override;
};

}  // namespace core
}  // namespace bw