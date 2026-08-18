#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Primitive.h"
#include "core/Stats.h"

namespace bw::core::arr {
inline constexpr int64_t FixedPointUnitsPerWorldUnit = 1000;

[[nodiscard]] inline int64_t ToFixedPointCoordinate(double coordinate) {
  return int64_t(std::llround(
      coordinate * double(FixedPointUnitsPerWorldUnit)));
}

[[nodiscard]] inline float ToWorldCoordinate(int64_t coordinate) {
  return float(double(coordinate) / double(FixedPointUnitsPerWorldUnit));
}

struct FixedPointVertex {
  int64_t x, y;

  bool operator==(FixedPointVertex const& other) const {
    return x == other.x && y == other.y;
  }
};

// A contour is implicitly closed from its last vertex back to its first.
// Its role as a shell or hole is derived from geometry and the fill rule.
using Contour = std::vector<FixedPointVertex>;

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
};

struct ContourInput {
  Contour contour;
  uint32_t primitiveIndex{~0u};
};

struct PSLG {
  std::vector<FixedPointVertex> vs;
  std::vector<Edge> es;
};

// Diagnostic counts for arrangement-construction performance tests and
// benchmarks. The exhaustive counts describe the work BuildPSLG performed
// before its grid broad phase was added.
struct PSLGConstructionStats {
  uint64_t segmentCount{0};
  uint64_t exhaustiveSegmentPairTests{0};
  uint64_t candidateSegmentPairTests{0};
  uint64_t candidatePointCount{0};
  uint64_t uniqueCandidatePointCount{0};
  uint64_t exhaustivePointSegmentTests{0};
  uint64_t candidatePointSegmentTests{0};
};

// Diagnostic counts for arrangement-hierarchy performance tests and
// benchmarks. The exhaustive count describes the candidate-box checks made by
// the former linear scan.
struct PolygonHierarchyStats {
  uint64_t exhaustiveCandidateBoxTests{0};
  uint64_t indexedCandidateBoxTests{0};
  uint64_t pointInCycleTests{0};
};

struct PolygonNode {
  int cycleIndex;
  int parent = -1;
  std::vector<int> children;
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
  // Vertices in traversal order, one for each outer-boundary edge.
  std::vector<uint32_t> outerBoundaryVertices;
  // Each nested vector is one explicit hole boundary, derived geometrically.
  std::vector<std::vector<uint32_t>> innerBoundaries;
  // Vertex traversal corresponding to each inner boundary.
  std::vector<std::vector<uint32_t>> innerBoundaryVertices;
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
using PrimitiveFoldOrder = std::vector<uint32_t>;

// Orders primitive-list indices by ascending priority. stable_sort preserves
// authored list order for equal priorities, as required by ADR-0001.
[[nodiscard]] PrimitiveFoldOrder BuildPrimitiveFoldOrder(
    std::vector<ArrangementPrimitive> const& primitives);

[[nodiscard]] bool EvaluateFold(
    std::vector<ArrangementPrimitive> const& primitives,
    Membership const& membership,
    PrimitiveFoldOrder const& foldOrder);

[[nodiscard]] ArrangementResultPtr BuildArrangement(
    std::vector<ArrangementPrimitive> const& primitives,
    ArrangementStats* stats = nullptr);

bool PointInFace(
    FixedPointVertex const& v,
    ArrangementFace const& face,
    ArrangementResult const& arrangement);

[[nodiscard]] std::vector<ArrangementTriangle> BuildArrangementTriangles(
    ArrangementResult const& arrangement);

[[nodiscard]] std::vector<ArrangementWall> BuildArrangementWalls(
    ArrangementResult const& arrangement);

PSLG BuildPSLG(
    std::vector<ContourInput> const& contours,
    PSLGConstructionStats* stats = nullptr);

std::vector<Cycle> ExtractMinimalCycles(PSLG const& graph);

std::vector<PolygonNode> BuildPolygonHierarchy(
    PSLG const& graph,
    std::vector<Cycle> const& cycles,
    PolygonHierarchyStats* stats = nullptr);

std::vector<Face> BuildFaces(std::vector<PolygonNode> const& nodes);
}  // namespace bw::core::arr
