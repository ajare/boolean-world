#pragma once

#include "willpower/common/Renderable.h"
#include "willpower/common/CachedStaticAccelerationGrid.h"
#include "willpower/common/ExtentsCalculator.h"

#include "willpower/geometry/Mesh.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Mesh.h"
#include "willpower/firepower/Bomb.h"
#include "willpower/firepower/BeamShard.h"

namespace WP_NAMESPACE {
namespace firepower {
class WP_FIREPOWER_API MeshCollisionManager : public Renderable {
  struct CollisionEdgeEntry {
    Vector2 v0, v1;
    uint32_t index;
  };

  struct Cell {
    std::vector<CollisionEdgeEntry> edgeEntries;
    std::set<uint32_t> vertexIndices;
  };

private:
  std::vector<Cell> mCells;

  int mCellsX, mCellsY;

  float mCellSizeX, mCellSizeY;

  Vector2 mOrigin, mSize;

  Mesh mMesh;

  // Bomb/beam collision variables
  mutable std::vector<Bomb::Line> mBombLines;

  mutable std::vector<int> mBombStartEnds;

  mutable std::vector<Vertex> mVerticesToCheck;

private:
  void addEdge(Vector2 const& v0, Vector2 const& v1, uint32_t v0Index, uint32_t v1Index, uint32_t edgeIndex, BoundingBox const& bounds);

  bool lineIntersectsMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Vertex const& ignoreVertex) const;

  Vector2 projectLineThroughMesh(Vector2 const& v0, Vector2 const& v1, Vertex const* ignoreVertex = nullptr, bool* isFull = nullptr) const;

public:
  MeshCollisionManager(ExtentsCalculator const& extents, uint32_t cellsX, uint32_t cellsY);

  ~MeshCollisionManager() = default;

  CachedStaticAccelerationGrid const* getGrid() const;

  Edge const& getCollisionEdge(uint32_t index) const;

  int getNumCollisionEdges() const;

  Mesh const& getMesh() const;

  void addMesh(geometry::Mesh const* mesh);

  void addMesh(geometry::Mesh const* mesh, geometry::Mesh::EdgeFilterFunction edgeFilterFn);

  void addStaticPolygon(std::vector<Vector2> const& points);

  void addStaticOBB(Vector2 const& minExtent, Vector2 const& maxExtent, float angle);

  void addStaticAABB(Vector2 const& minExtent, Vector2 const& maxExtent);

  void getExtents(Vector2& minExtent, Vector2& maxExtent) const;

  int32_t checkBullet(Vector2 const& oldPosition, Vector2 const& newPosition, float radius, float checkTunnelling);

  static std::vector<Bomb::Arc> getFullBomb(Bomb const& bomb);

  std::vector<Bomb::Arc> checkBomb(Bomb const& bomb) const;

  static std::vector<BeamShard> getFullBeam(Vector2 const& position, float angle, float width, float length);

  std::vector<BeamShard> checkBeam(Vector2 const& position, float angle, float width, float length) const;
};

}  // namespace firepower
}  // namespace WP_NAMESPACE