#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include "core/Primitive.h"
#include "core/Clipper2Polygon.h"

namespace expr {
struct Vertex {
  double x, y;

  bool operator==(Vertex const& other) const {
    return x == other.x && y == other.y;
  }
};

struct Edge {
  int vi[2];
  int fi[2] = {-1, -1};

  bool doubleSided() const {
    return fi[0] >= 0 && fi[1] >= 0;
  }
};

struct Cycle {
  std::vector<int> vis;
  std::vector<int> eis;

  double area;
  std::vector<uint32_t> primitiveIndices;
  Vertex interiorPoint{};
  bool bounded{true};
};

struct Face {
  int polygon;
  std::vector<int> holes;

  // If the face is owned by a non-hole polygon, then owningPolygon
  // is set.  Otherwise holePolygon is set.
  int owningPolygon{-1};
  int holePolygon{-1};
};

struct PSLG {
  std::vector<Vertex> vs;
  std::vector<Edge> es;
};

struct PolygonNode {
  int cycleIndex;
  int parent = -1;
  std::vector<int> children;
};

struct FaceTriangle {
  int vi[3];
  int fi;
};

bool PointInFace(Vertex const& v, Face const& face, std::vector<Cycle> const& cycles, PSLG const& graph);

PSLG BuildPSLG(std::vector<bw::core::Clipper2Polygon> const& polygons, std::vector<bw::core::Primitive*> const& primitives);

std::vector<Cycle> ExtractMinimalCycles(PSLG const& graph);

std::vector<PolygonNode> BuildPolygonHierarchy(PSLG const& graph, std::vector<Cycle>& cycles);

std::vector<Face> BuildFaces(std::vector<PolygonNode> const& nodes, std::vector<Cycle> const& cycles);

std::vector<Face> CalculateOwningPolygons(std::vector<Face> const& faces, std::vector<bw::core::Clipper2Polygon> const& polygons, std::vector<Cycle> const& cycles, PSLG& graph, std::vector<bw::core::Primitive*> const& primitives);

std::vector<FaceTriangle> BuildFaceTriangles(std::vector<Face> const& faces, std::vector<Cycle> const& cycles, PSLG const& graph);
}  // namespace expr
