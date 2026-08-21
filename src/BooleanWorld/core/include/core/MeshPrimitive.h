#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <willpower/geometry/Mesh.h>

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/Vertex.h"

namespace bw {
namespace core {

struct MeshFilledRegion;
class MeshPrimitiveEditingProxy;

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
  friend class MeshPrimitiveEditingProxy;

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

  // One independently fillable MeshPrimitive for every Shell and Island in
  // deterministic pre-order. Each output retains that filled region and its
  // direct Holes, copies this Primitive's authored state, and is a Union at
  // this Primitive's priority. An empty result means this has fewer than two
  // filled regions and must not be decomposed.
  [[nodiscard]] std::vector<MeshPrimitive*> decomposeFilledRegions() const;

  // Builds the hierarchy-aware, rest-pose world-space editing authority.
  // Its wrapped Mesh is exposed only as const; every mutation is routed through
  // MeshPrimitiveEditingProxy so welded topology and hierarchy cannot diverge.
  [[nodiscard]] std::unique_ptr<MeshPrimitiveEditingProxy> createEditingProxy() const;
  // Temporary name retained while downstream callers migrate; it returns the
  // same specialized proxy, never a mutable geometry Mesh.
  [[nodiscard]] std::unique_ptr<MeshPrimitiveEditingProxy> createGeometryProxy() const {
    return createEditingProxy();
  }
  bool retainRing(uint32_t complexPolygonIndex, uint32_t ringIndex);

  Primitive* copy() const override;
  std::string getType() const override;
  std::string getName() const override;
  float getRadius() const override;
};

class BW_API MeshPrimitiveEditingProxy {
public:
  enum class NodeRole { Shell,
                        Hole,
                        Island };

  struct NodeMapping {
    uint32_t polygonIndex;
    NodeRole role;
    uint32_t parentPolygonIndex;
  };

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;

  explicit MeshPrimitiveEditingProxy(MeshPrimitive const& primitive);
  bool mutateRings(std::function<bool(ClosedPolygon&)> mutation);
  friend class MeshPrimitive;

public:
  ~MeshPrimitiveEditingProxy();
  MeshPrimitiveEditingProxy(MeshPrimitiveEditingProxy&&) noexcept;
  MeshPrimitiveEditingProxy& operator=(MeshPrimitiveEditingProxy&&) noexcept;
  MeshPrimitiveEditingProxy(MeshPrimitiveEditingProxy const&);
  MeshPrimitiveEditingProxy& operator=(MeshPrimitiveEditingProxy const&);

  [[nodiscard]] wp::geometry::Mesh const& getMesh() const;
  [[nodiscard]] std::vector<NodeMapping> getNodeMappings() const;
  operator wp::geometry::Mesh const&() const { return getMesh(); }

  // Read-only forwarding keeps query-heavy editor code concise without ever
  // exposing a mutable Mesh reference.
  [[nodiscard]] uint32_t getFirstVertexIndex() const { return getMesh().getFirstVertexIndex(); }
  [[nodiscard]] uint32_t getNextVertexIndex(uint32_t index) const { return getMesh().getNextVertexIndex(index); }
  [[nodiscard]] bool vertexIndexIterationFinished(uint32_t index) const { return getMesh().vertexIndexIterationFinished(index); }
  [[nodiscard]] wp::geometry::Vertex const& getVertex(uint32_t index) const { return getMesh().getVertex(index); }
  [[nodiscard]] uint32_t getFirstEdgeIndex() const { return getMesh().getFirstEdgeIndex(); }
  [[nodiscard]] uint32_t getNextEdgeIndex(uint32_t index) const { return getMesh().getNextEdgeIndex(index); }
  [[nodiscard]] bool edgeIndexIterationFinished(uint32_t index) const { return getMesh().edgeIndexIterationFinished(index); }
  [[nodiscard]] wp::geometry::Edge const& getEdge(uint32_t index) const { return getMesh().getEdge(index); }
  [[nodiscard]] uint32_t getFirstPolygonIndex() const { return getMesh().getFirstPolygonIndex(); }
  [[nodiscard]] uint32_t getNextPolygonIndex(uint32_t index) const { return getMesh().getNextPolygonIndex(index); }
  [[nodiscard]] bool polygonIndexIterationFinished(uint32_t index) const { return getMesh().polygonIndexIterationFinished(index); }
  [[nodiscard]] wp::geometry::Polygon const& getPolygon(uint32_t index) const { return getMesh().getPolygon(index); }
  [[nodiscard]] wp::geometry::IndexSet getVertexIndicesInBoundingBox(wp::BoundingBox const& bounds) const { return getMesh().getVertexIndicesInBoundingBox(bounds); }
  void getExtents(wp::Vector2& minimum, wp::Vector2& maximum) const { getMesh().getExtents(minimum, maximum); }

  // Geometry-only candidates may be prepared by validators and installed as
  // one mutation. The hierarchy mapping is retained and checked first.
  bool replaceMesh(wp::geometry::Mesh mesh);
  void moveVertex(uint32_t vertexIndex, wp::Vector2 const& delta);
  void moveVertices(wp::geometry::IndexVector const& vertexIndices, wp::Vector2 const& delta);
  void moveEdge(uint32_t edgeIndex, wp::Vector2 const& delta);
  void moveRing(uint32_t polygonIndex, wp::Vector2 const& delta);
  bool splitEdge(uint32_t edgeIndex, wp::geometry::SplitEdgeResult* result = nullptr);
  bool removeVertex(uint32_t vertexIndex);
  bool removeEdge(uint32_t edgeIndex);
  bool removeRing(uint32_t polygonIndex);

  [[nodiscard]] uint32_t addShell(ClosedPolygon ring);
  [[nodiscard]] uint32_t addHole(uint32_t filledPolygonIndex, ClosedPolygon ring);
  [[nodiscard]] uint32_t addIsland(uint32_t holePolygonIndex, ClosedPolygon ring);
  [[nodiscard]] uint32_t fillHole(uint32_t holePolygonIndex);

  // Builds and validates a complete local-space candidate before replacing
  // authored storage. On failure the Primitive and all derived geometry are
  // unchanged. Primitive transforms are never modified.
  void commitTo(MeshPrimitive& primitive) const;
};

}  // namespace core
}  // namespace bw
