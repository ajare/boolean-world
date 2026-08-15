#include <mapbox/earcut.hpp>

#include "core/Triangulator.h"
#include "core/World.h"

namespace bw {
namespace core {
using namespace std;

Triangulator::Triangulator(World const* world, bool globalBounds, bool perTriangleBounds, bool removeDuplicates, wp::AccelerationGrid* grid)
    : mwWorld(world), mGlobalBounds(globalBounds), mPerTriangleBounds(perTriangleBounds), mRemoveDuplicates(removeDuplicates), mGrid(grid) {
  mTriangulation.bounds.setPosition(1e10f, 1e10f);
  mTriangulation.bounds.setSize(-2e10f, -2e10f);
}

Triangulation const& Triangulator::getTriangulation() const {
  return mTriangulation;
}

void Triangulator::processPolygon(ClippedPolygon const& clippedPolygon, vector<TriangulationData>& triangulationData, ClosedPolygon& vertices, TriangulationStats* stats) {
  if (!clippedPolygon.isHole) {
    // New polygon: time to calculate the current one
    if (!triangulationData.empty()) {
      _triangulate(triangulationData, vertices, stats);

      vertices.clear();
      triangulationData.clear();
    }
  }

  triangulationData.push_back({clippedPolygon.vertices, clippedPolygon.primitiveIndex});

  // Store vertices in contiguous list for later indexing
  copy(clippedPolygon.vertices.begin(), clippedPolygon.vertices.end(), back_inserter(vertices));
}

vector<array<float, 2>> Triangulator::convertPolygon(ClosedPolygon const& polygon) {
  using Point = array<float, 2>;

  vector<Point> result;

  for (auto const& vertex : polygon) {
    result.push_back({vertex.p.x, vertex.p.y});
  }

  return result;
}

void Triangulator::_triangulate(vector<TriangulationData> const& triangulationData, VertexList const& vertices, TriangulationStats* stats) {
  // Convert Vertex to array<float, 2> for the earcutter
  using Point = array<float, 2>;

  vector<vector<Point>> inputPolygons;

  auto primitiveIndex = triangulationData[0].primitiveIndex;

  for (auto const& triangulation : triangulationData) {
    vector<Point> polygonToProcess = convertPolygon(triangulation.triangulation);
    inputPolygons.push_back(polygonToProcess);
  }

  vector<uint32_t> triangleIndices = mapbox::earcut<uint32_t>(inputPolygons);

  auto numTriangleIndices = (uint32_t)triangleIndices.size();

  // TODO: create adjacency map here.  For any given triangle: what are its neighbours? (0-3)
  //       we also want to be able to quickly check if we have visited these neighbours already

  for (uint32_t i = 0; i < numTriangleIndices; i += 3) {
    auto ti0 = triangleIndices[i + 0];
    auto ti1 = triangleIndices[i + 1];
    auto ti2 = triangleIndices[i + 2];

    auto const& v0 = vertices[ti0].p;
    auto const& v1 = vertices[ti1].p;
    auto const& v2 = vertices[ti2].p;

    if (mRemoveDuplicates) {
      // Use a map of triangle centre to index in triangulation list
      // Calculate the centre and see if it already exists.
      // This assume that no triangles in triangulation are intended to overlap!
      auto triangleIndex = (uint32_t)mTriangulation.tris.size();

      auto triCentre = (v0 + v1 + v2) / 3.0f;
      auto triKey = make_pair(triCentre.x, triCentre.y);

      auto [it, inserted] = mTriCentreLookup.insert(make_pair(triKey, triangleIndex));

      if (!inserted) {
        // Something already there.  Do we need to replace it with this triangle?
        auto curTriangleIndex = it->second;
        auto& triangle = mTriangulation.tris[curTriangleIndex];

        auto curPriority = mwWorld->getPrimitive(triangle.primitiveIndex)->getPriority();
        auto newPriority = mwWorld->getPrimitive(primitiveIndex)->getPriority();

        if (newPriority > curPriority) {
          triangle.primitiveIndex = primitiveIndex;
        }

        // Don't add this triangle to the triangulation, as it has already been inserted
        continue;
      }
    }

    wp::BoundingBox triBounds;

    if (mPerTriangleBounds || mGrid) {
      // Calculate triangle bounds for lookup
      float xMin = min(v0.x, min(v1.x, v2.x));
      float yMin = min(v0.y, min(v1.y, v2.y));
      float xMax = max(v0.x, max(v1.x, v2.x));
      float yMax = max(v0.y, max(v1.y, v2.y));

      triBounds = wp::BoundingBox(xMin, yMin, xMax - xMin, yMax - yMin);

      if (mGrid) {
        mGrid->addItem(i / 3, triBounds);
      }
    }

    mTriangulation.tris.push_back({{vertices[ti0], vertices[ti1], vertices[ti2]}, triBounds, primitiveIndex});
  }

  // Bounds
  if (mGlobalBounds) {
    mTriangulation.calculateBounds();
  }

  if (stats) {
    stats->trianglesGenerated += (uint32_t)mTriangulation.tris.size();
  }
}

TriangulationStats Triangulator::execute(vector<ClippedPolygon> const& polygons) {
  vector<TriangulationData> triangulationData;
  ClosedPolygon triVertList;
  TriangulationStats stats;

  for (auto const& polygon : polygons) {
    processPolygon(polygon, triangulationData, triVertList, &stats);
  }

  // Triangulate final part
  _triangulate(triangulationData, triVertList, &stats);

  return stats;
}

}  // namespace core
}  // namespace bw
