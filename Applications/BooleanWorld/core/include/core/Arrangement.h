#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Clipper2Polygon.h"
#include "core/Primitive.h"

namespace bw::core::arr {
inline constexpr int64_t FixedPointUnitsPerWorldUnit = 1000;

struct FixedPointVertex {
  int64_t x, y;

  bool operator==(FixedPointVertex const& other) const {
    return x == other.x && y == other.y;
  }
};

// A contour is implicitly closed from its last vertex back to its first.
// Its role as a shell or hole is derived from geometry and the fill rule.
using Contour = std::vector<FixedPointVertex>;

// Temporary prototype name retained while existing arrangement consumers move.
using Vertex = FixedPointVertex;

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
  // is set. Otherwise holePolygon is set.
  int owningPolygon{-1};
  int holePolygon{-1};
};

struct ContourInput {
  Contour contour;
  uint32_t primitiveIndex{~0u};
};

struct PSLG {
  std::vector<FixedPointVertex> vs;
  std::vector<Edge> es;
  std::vector<Contour> sourceContours;
  // Temporary metadata used only by the legacy Clipper2 test adapter.
  std::vector<bool> legacySourceContourIsHole;
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
  std::vector<Contour> contours;
  Primitive::Operation operation;
  Primitive::FillRule fillRule;
  uint8_t priority;
  uint32_t primitiveIndex;
  PrimitivePropertySet properties{};
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
  // Each nested vector is one explicit hole boundary, derived geometrically.
  std::vector<std::vector<uint32_t>> innerBoundaries;
  Membership membership;
  bool solid{false};
  uint16_t paletteIndex{0};
  uint32_t primitiveIndex{~0u};
};

struct ArrangementResult {
  std::vector<FixedPointVertex> vertices;
  std::vector<ArrangementEdge> edges;
  std::vector<ArrangementFace> faces;
  std::vector<PrimitivePropertySet> palette;
};

using ArrangementResultPtr = std::shared_ptr<ArrangementResult const>;

[[nodiscard]] bool EvaluateFold(
    std::vector<ArrangementPrimitive> const& primitives,
    Membership const& membership);

[[nodiscard]] ArrangementResultPtr BuildArrangement(
    std::vector<ArrangementPrimitive> const& primitives);

bool PointInFace(
    FixedPointVertex const& v,
    ArrangementFace const& face,
    ArrangementResult const& arrangement);

bool PointInFace(
    FixedPointVertex const& v,
    Face const& face,
    std::vector<Cycle> const& cycles,
    PSLG const& graph);

[[nodiscard]] std::vector<ArrangementTriangle> BuildArrangementTriangles(
    ArrangementResult const& arrangement);

[[nodiscard]] std::vector<ArrangementWall> BuildArrangementWalls(
    ArrangementResult const& arrangement);

PSLG BuildPSLG(std::vector<ContourInput> const& contours);

// Temporary Clipper2 input adapter retained until existing tests migrate.
PSLG BuildPSLG(
    std::vector<Clipper2Polygon> const& polygons,
    std::vector<Primitive*> const& primitives);

std::vector<Cycle> ExtractMinimalCycles(PSLG const& graph);

std::vector<PolygonNode> BuildPolygonHierarchy(
    PSLG const& graph,
    std::vector<Cycle>& cycles);

std::vector<Face> BuildFaces(
    std::vector<PolygonNode> const& nodes,
    std::vector<Cycle> const& cycles);

std::vector<Face> CalculateOwningPolygons(
    std::vector<Face> const& faces,
    std::vector<Clipper2Polygon> const& polygons,
    std::vector<Cycle> const& cycles,
    PSLG& graph,
    std::vector<Primitive*> const& primitives);

std::vector<FaceTriangle> BuildFaceTriangles(
    std::vector<Face> const& faces,
    std::vector<Cycle> const& cycles,
    PSLG const& graph);
}  // namespace bw::core::arr

// Temporary namespace compatibility for consumers of the arrangement prototype.
namespace expr {
using namespace bw::core::arr;
}
