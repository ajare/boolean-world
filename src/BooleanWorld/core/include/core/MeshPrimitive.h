#pragma once

#include <memory>
#include <vector>

#include <willpower/geometry/Mesh.h>

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/Vertex.h"

namespace bw {
namespace core {

struct MeshFilledRegion;

// A Hole is an empty Ring directly contained by a filled region. Its children
// are Islands; the types make non-alternating links unrepresentable.
struct BW_API MeshHole {
  ClosedPolygon ring;
  std::vector<MeshFilledRegion> islands;
};

// At the root this is a Shell; beneath a Hole it is an Island.
struct BW_API MeshFilledRegion {
  ClosedPolygon ring;
  std::vector<MeshHole> holes;
};

class BW_API MeshPrimitive : public Primitive {
  friend class Primitive;  // Only Primitive::instantiate constructs for loading.

  std::vector<MeshFilledRegion> mShells;

  struct LocalTreeTag {};

  MeshPrimitive(Operation operation, std::vector<MeshFilledRegion> shells, LocalTreeTag);

  void replaceTree(std::vector<MeshFilledRegion> shells);

protected:
  MeshPrimitive();

  void copyFrom(MeshPrimitive const& other);

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

  std::vector<ComplexPolygon> generateVerticesImpl() override;

  void polygonsUpdated() override;

  void rotateAuthoredGeometry(float angle, wp::Vector2 const& origin) override;

public:
  // Temporary shallow compatibility constructor. Every ComplexPolygon is one
  // root Shell followed by its direct Holes; cross-entry nesting is rejected.
  MeshPrimitive(Operation operation, FillRule fillType, std::vector<ComplexPolygon> const& polygons);

  MeshPrimitive(MeshPrimitive const& other);

  MeshPrimitive& operator=(MeshPrimitive const& other);

  // Tree-native construction accepts world-space Rings, normalizes them into
  // MeshPrimitive-local coordinates, and commits only after full validation.
  [[nodiscard]] static MeshPrimitive* fromTree(
      Operation operation, std::vector<MeshFilledRegion> shells);

  // Temporary shallow converter for flat producers. It never infers nesting.
  [[nodiscard]] static MeshPrimitive* fromComplexPolygons(
      Operation operation,
      FillRule fillType,
      std::vector<ComplexPolygon> polygons);

  // Authoritative topology is const-only. Sibling order is authored order and
  // traversal can continue to arbitrary depth through holes/islands.
  [[nodiscard]] std::vector<MeshFilledRegion> const& getShells() const;

  // Deterministic pre-order derived form: one ComplexPolygon per Shell or
  // Island, with only that filled region's direct Hole Rings.
  [[nodiscard]] std::vector<ComplexPolygon> flattenTree() const;

  // Temporary ADR-0016 editing compatibility entry points.
  [[nodiscard]] std::unique_ptr<wp::geometry::Mesh> createGeometryProxy() const;
  void updateFromGeometryProxy(wp::geometry::Mesh const& mesh);
  bool retainRing(uint32_t complexPolygonIndex, uint32_t ringIndex);

  Primitive* copy() const override;
  std::string getType() const override;
  std::string getName() const override;
  float getRadius() const override;
};

}  // namespace core
}  // namespace bw
