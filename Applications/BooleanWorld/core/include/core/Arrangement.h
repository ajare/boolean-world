#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <clipper2/clipper.h>

#include "core/Primitive.h"
#include "core/Clipper2Polygon.h"

namespace expr {
struct Vertex {
  int64_t x, y;

  bool operator==(Vertex const& other) const {
    return x == other.x && y == other.y;
  }
};

struct WindingDelta {
  uint32_t primitiveIndex;
  // Change when crossing from the edge's right face to its left face.
  int32_t delta;
};

struct Edge {
  int vi[2];
  // Left and right faces relative to vi[0] -> vi[1].
  int fi[2] = {-1, -1};
  std::vector<WindingDelta> windingDeltas;

  bool doubleSided() const {
    return fi[0] >= 0 && fi[1] >= 0;
  }
};

struct Cycle {
  std::vector<int> vis;
  std::vector<int> eis;

  int64_t area;
  std::vector<uint32_t> primitiveIndices;
};

class Membership {
  std::vector<uint64_t> mWords;

public:
  explicit Membership(size_t primitiveCount = 0);

  void set(size_t primitiveIndex, bool value = true);

  [[nodiscard]] bool contains(size_t primitiveIndex) const;

  bool operator==(Membership const& other) const = default;
};

struct Face {
  int polygon;
  std::vector<int> holes;
  Membership membership;
  bool solid{false};

  // If the face is owned by a non-hole polygon, then owningPolygon
  // is set.  Otherwise holePolygon is set.
  int owningPolygon{-1};
  int holePolygon{-1};
};

struct PSLG {
  std::vector<Vertex> vs;
  std::vector<Edge> es;
  std::vector<Clipper2Lib::Path64> sourceContours;
  std::vector<bool> sourceContourIsHole;
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

struct ArrangementTriangle {
  uint32_t v[3];
  uint32_t face;
};

enum struct ArrangementWallKind : uint8_t {
  Border,
  FloorStep,
  CeilingStep
};

struct ArrangementWall {
  uint32_t edge;
  float minZ;
  float maxZ;
  uint16_t paletteIndex;
  ArrangementWallKind kind;
};

struct ArrangementPrimitive {
  std::vector<Clipper2Lib::Path64> contours;
  bw::core::Primitive::Operation operation;
  bw::core::Primitive::FillRule fillRule;
  uint8_t priority;
  uint32_t primitiveIndex;
  bw::core::PrimitivePropertySet properties{};
};

struct ArrangementEdge {
  uint32_t v[2];
  // Left and right faces relative to v[0] -> v[1].
  uint32_t face[2];
};

struct ArrangementFace {
  // Edge indices. Bounded faces have one CCW outer boundary; the unbounded
  // exterior face at index zero has no outer boundary.
  std::vector<uint32_t> outerBoundary;
  // Each nested vector is one explicit hole boundary.
  std::vector<std::vector<uint32_t>> innerBoundaries;
  Membership membership;
  bool solid{false};
  uint16_t paletteIndex{0};
  uint32_t primitiveIndex{~0u};
};

struct ArrangementResult {
  std::vector<Vertex> vertices;
  std::vector<ArrangementEdge> edges;
  std::vector<ArrangementFace> faces;
  std::vector<bw::core::PrimitivePropertySet> palette;
};

using ArrangementResultPtr = std::shared_ptr<ArrangementResult const>;

[[nodiscard]] bool EvaluateFold(
    std::vector<ArrangementPrimitive> const& primitives,
    Membership const& membership);

[[nodiscard]] ArrangementResultPtr BuildArrangement(
    std::vector<ArrangementPrimitive> const& primitives);

bool PointInFace(
    Vertex const& v,
    ArrangementFace const& face,
    ArrangementResult const& arrangement);

bool PointInFace(Vertex const& v, Face const& face, std::vector<Cycle> const& cycles, PSLG const& graph);

[[nodiscard]] std::vector<ArrangementTriangle> BuildArrangementTriangles(
    ArrangementResult const& arrangement);

[[nodiscard]] std::vector<ArrangementWall> BuildArrangementWalls(
    ArrangementResult const& arrangement);

PSLG BuildPSLG(std::vector<bw::core::Clipper2Polygon> const& polygons, std::vector<bw::core::Primitive*> const& primitives);

std::vector<Cycle> ExtractMinimalCycles(PSLG const& graph);

std::vector<PolygonNode> BuildPolygonHierarchy(PSLG const& graph, std::vector<Cycle>& cycles);

std::vector<Face> BuildFaces(std::vector<PolygonNode> const& nodes, std::vector<Cycle> const& cycles);

std::vector<Face> CalculateOwningPolygons(std::vector<Face> const& faces, std::vector<bw::core::Clipper2Polygon> const& polygons, std::vector<Cycle> const& cycles, PSLG& graph, std::vector<bw::core::Primitive*> const& primitives);

std::vector<FaceTriangle> BuildFaceTriangles(std::vector<Face> const& faces, std::vector<Cycle> const& cycles, PSLG const& graph);
}  // namespace expr
