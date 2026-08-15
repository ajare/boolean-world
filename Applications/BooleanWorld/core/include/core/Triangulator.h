#pragma once

#include <vector>
#include <array>
#include <map>

#include <willpower/common/AccelerationGrid.h>

#include "core/Platform.h"
#include "core/Vertex.h"
#include "core/Clipper.h"
#include "core/Triangulation.h"
#include "core/Stats.h"

namespace bw {
namespace core {
class World;

struct TriangulationData {
  ClosedPolygon triangulation;
  uint32_t primitiveIndex;
};

class BW_API Triangulator {
  World const* mwWorld;

  bool mGlobalBounds, mPerTriangleBounds, mRemoveDuplicates;

  wp::AccelerationGrid* mGrid;

  Triangulation mTriangulation;

  std::map<std::pair<float, float>, uint32_t> mTriCentreLookup;

private:
  static std::vector<std::array<float, 2>> convertPolygon(ClosedPolygon const& polygon);

  void processPolygon(ClippedPolygon const& clippedPolygon, std::vector<TriangulationData>& triangulationData, VertexList& vertices, TriangulationStats* stats);

public:
  explicit Triangulator(World const* world, bool globalBounds, bool perTriangleBounds, bool removeDuplicates, wp::AccelerationGrid* grid = nullptr);

  Triangulation const& getTriangulation() const;

  void _triangulate(std::vector<TriangulationData> const& triangulationData, ClosedPolygon const& vertices, TriangulationStats* stats);

  TriangulationStats execute(std::vector<ClippedPolygon> const& polygons);
};

}  // namespace core
}  // namespace bw