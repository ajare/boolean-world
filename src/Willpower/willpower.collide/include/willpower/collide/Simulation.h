#pragma once

#include <memory>
#include <set>

#include "willpower/common/Vector2.h"
#include "willpower/common/AccelerationGrid.h"
#include "willpower/common/ExtentsCalculator.h"

#include "willpower/collide/Platform.h"
#include "willpower/collide/Collider.h"
#include "willpower/collide/StaticLine.h"
#include "willpower/collide/SweepResult.h"

#include "willpower/geometry/Mesh.h"

namespace WP_NAMESPACE {
namespace collide {

class WP_COLLIDE_API Simulation {
  std::set<Collider*> mColliders;

  int32_t mNextIndex;

  AccelerationGrid* mCollidersGrid;

  AccelerationGrid* mStaticLinesGrid;

  void* mwUserObject;

  Vector2 mMinExtent, mMaxExtent;

  // Stats
  int mNumSweepChecks;

protected:
  std::vector<StaticLine> mStaticLines;

private:
  void destroyColliders();

  void createGrids(Vector2 const& minExtent, Vector2 const& maxExtent, float cellSizeX, float cellSizeY);

  void destroyGrids();

  void sweepCollider(Collider* collider, Vector2 const& desiredMovement, uint32_t sweepCount = 0);

  virtual std::set<uint32_t> getLineIndices(wp::BoundingBox const& bounds) const;

public:
  Simulation(ExtentsCalculator const& extents, uint32_t cellsX, uint32_t cellsY, void* userObj = nullptr);

  Simulation(Simulation const&) = delete;

  Simulation& operator=(Simulation const&) = delete;

  virtual ~Simulation();

  void getExtents(Vector2& minExtent, Vector2& maxExtent);

  int32_t addCollider(std::unique_ptr<Collider> collider);

  void removeCollider(Collider* collider);

  size_t getNumColliders() const;

  std::vector<Collider*> getColliders() const;

  void addMesh(geometry::Mesh const* mesh);

  void addMesh(geometry::Mesh const* mesh, geometry::Mesh::EdgeFilterFunction edgeFilterFn);

  virtual std::pair<uint32_t, uint32_t> addStaticLine(float x0, float y0, float x1, float y1, bool doubleSided);

  virtual std::pair<uint32_t, uint32_t> addStaticLine(Vector2 const& v0, Vector2 const& v1, bool doubleSided);

  StaticLine const& getStaticLine(uint32_t index) const;

  void clearStaticLines();

  AccelerationGrid const* getStaticLinesGrid() const;

  AccelerationGrid const* getCollidersGrid() const;

  uint32_t getNumStaticLines() const;

  void enableStaticLines(uint32_t offset, uint32_t count, bool enabled);

  void update(float frameTime);

  // Queries
  int getNumSweepChecks() const;

  bool colliderIntersects(Collider const* collider) const;

  bool projectCollider(Collider const* collider, Vector2 const& desiredMovement, SweepResult* result);

  bool projectLine(Vector2 const& v0, Vector2 const& v1, SweepResult* result);
};

}  // namespace collide
}  // namespace WP_NAMESPACE
