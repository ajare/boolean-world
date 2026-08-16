#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/Vertex.h"
#include "core/Clipper.h"

namespace bw {
namespace core {

class BW_API MeshPrimitive : public Primitive {
  friend class World;  // Only World can call the default constructor (during deserialization)

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

  static MeshPrimitive* fromClippedPolygons(Operation operation, FillRule fillType, std::vector<ClippedPolygon> const& polygons);

  Primitive* copy() const override;

  std::string getType() const override;

  std::string getName() const override;

  float getRadius() const override;
};

}  // namespace core
}  // namespace bw