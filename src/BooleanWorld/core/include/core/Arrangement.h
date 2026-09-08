#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/ChipGenerationParameters.h"
#include "core/Elevation.h"
#include "core/Primitive.h"
#include "core/Stats.h"
#include "core/WallMaskOverride.h"
#include "core/WallNormalMapOverride.h"

namespace bw::core::arr {
inline constexpr int64_t FixedPointUnitsPerWorldUnit = 1000;

[[nodiscard]] inline int64_t ToFixedPointCoordinate(double coordinate) {
  return int64_t(std::llround(
      coordinate * double(FixedPointUnitsPerWorldUnit)));
}

[[nodiscard]] inline float ToWorldCoordinate(int64_t coordinate) {
  return float(double(coordinate) / double(FixedPointUnitsPerWorldUnit));
}

// Converts a doubled, signed fixed-point shoelace area (as produced by
// Cycle::area or FaceArea2's convention) into a world-space area.
[[nodiscard]] inline double ToWorldArea(int64_t area2) {
  return double(area2) /
         (2.0 * double(FixedPointUnitsPerWorldUnit) * double(FixedPointUnitsPerWorldUnit));
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
  // Wall collision/visibility overrides. Non-Mesh segments never carry a
  // value. For coincident Mesh contributors, an authored collision false
  // dominates true while unset contributes nothing; visibility retains the
  // first Mesh-sourced value.
  std::optional<bool> collidesOverride;
  std::optional<bool> visibleOverride;
  std::optional<WallNormalMapOverride> normalMapOverride;
  std::optional<WallMaskOverride> wallMaskOverride;

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
  // Per-edge wall collision/visibility overrides, parallel to contour: index
  // i is the override for the edge from contour[i] to contour[(i+1)%size()].
  // May be shorter than contour, or empty (meaning no overrides at all); any
  // out-of-range index is treated as std::nullopt.
  std::vector<std::optional<bool>> edgeOverrides{};
  std::vector<std::optional<bool>> edgeVisibleOverrides{};
  std::vector<std::optional<WallNormalMapOverride>> edgeNormalMapOverrides{};
  std::vector<std::optional<WallMaskOverride>> edgeWallMaskOverrides{};
  // Structural primitives participate in the fold but cannot select a wall
  // normal-map or wall-mask value.
  bool contributesProperties{true};
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

// One side of a triangulated Arrangement face. Elevations correspond to the
// owning ArrangementTriangle's v entries; normal is one flat geometric normal
// shared by all three vertices.
struct ArrangementTriangleSurface {
  std::array<float, 3> elevation{};
  std::array<float, 3> normal{};
};

struct ArrangementTriangle {
  uint32_t v[3];
  uint32_t face;
  ArrangementTriangleSurface floor{};
  ArrangementTriangleSurface ceiling{};
};

// One triangulated piece of an Arrangement face used by Liquid settlement.
// Its floor and ceiling remain affine over the World-plane triangle: storing
// their values at the corners is sufficient to integrate the vertical column
// exactly and does not add elevation-only points to Arrangement topology.
struct HydraulicCell {
  ArrangementTriangle triangle;
  std::array<wp::Vector2, 3> positions{};
  Elevation floor;
  Elevation ceiling;
  double worldArea{};

  // Integrated Liquid volume this cell can hold below one horizontal
  // elevation, clamped locally between its affine floor and ceiling. Returns
  // zero below the floor and the cell's full capacity above the ceiling.
  [[nodiscard]] double volumeBelow(double elevation) const;
  [[nodiscard]] double capacityBelow(double elevation) const {
    return volumeBelow(elevation);
  }
};

// One triangle of a clipped, horizontal visible Liquid interface. A wet cell
// can emit more than one of these after clipping, or none when it is dry or
// completely flooded above its ceiling.
struct LiquidSurfaceTriangle {
  std::array<wp::Vector2, 3> positions{};
  float elevation{};
  uint32_t cell{};
  uint32_t face{};
};

// Immutable result of the post-Arrangement Liquid pass. Pool elevations are
// parallel to cells and may repeat for every cell in one Pool. Negative
// infinity means no Liquid occupies that cell's basin; a finite elevation can
// still leave the whole local cell dry above the shoreline. faceDepths retains
// the flat-world compatibility view used by older callers; position-based
// queries use poolElevations and evaluate the local floor instead.
struct LiquidState {
  std::vector<HydraulicCell> cells;
  std::vector<double> poolElevations;
  std::vector<LiquidSurfaceTriangle> surfaceTriangles;
  std::vector<float> faceDepths;
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
  // Minimum vertical headroom available along this derived segment: the
  // overlap of the two adjacent solid faces' evaluated floor/ceiling ranges. For a
  // zero-gradient World this is the previous face-wide value. Meaningless for
  // Border, which always blocks regardless.
  float clearance;
  // Whether this wall renders. Resolved directly from the source edge's
  // visibleOverride (defaulting true) - unlike collision, visibility needs
  // no world-level parameter, so it is resolved here rather than deferred
  // to ArrangementWorldData.
  bool visible{true};
  WallNormalMapOverride normalMapOverride{};
  WallMaskOverride wallMaskOverride{};
  // Evaluated vertical boundaries at sourceEdgeParameter[0] and [1] along
  // ArrangementEdge::v[0] -> v[1]. A Step surface is split into separate
  // derived segments when its adjacent planes cross, without introducing an
  // Arrangement vertex. minZ/maxZ are conservative bounds retained for
  // flat-compatible consumers.
  std::array<float, 2> bottomZ{};
  std::array<float, 2> topZ{};
  std::array<float, 2> sourceEdgeParameter{0.0f, 1.0f};
  // The canonical front side and the face whose Primitive owns this segment's
  // wall material. They differ for Steps: FloorStep faces the lower floor and
  // is owned by the higher floor; CeilingStep faces the higher ceiling and is
  // owned by the lower ceiling. A Difference-owned Border can likewise have
  // an empty ownerFace while facing its adjacent solid face.
  uint32_t frontFace{~0u};
  uint32_t ownerFace{~0u};
};

struct ArrangementAudioEmitter {
  wp::Vector2 position;
  float heightOffset{0.0f};
  std::string soundId;
  std::string guid;
  float cullRadius{0.0f};
  std::optional<EmitterPlacementKey> placementKey;
  // Index into the current generation's ArrangementPrimitive list, matching
  // ArrangementFace::membership (not Primitive::getId()).
  uint32_t parentPrimitiveIndex{~0u};
};

struct ArrangementPrimitive {
  std::vector<Contour> contours;
  Primitive::Operation operation;
  Primitive::FillRule fillRule;
  uint64_t priority;
  uint32_t primitiveIndex;
  PrimitivePropertySet properties{};
  // Per-contour, per-edge wall collision/visibility overrides, parallel to
  // contours: contourEdgeOverrides[c][i] is the override for the edge from
  // contours[c][i] to contours[c][(i+1)%contours[c].size()]. May be shorter
  // than contours, a contour's inner vector may be shorter than its vertex
  // count, or the whole field may be empty (meaning no overrides at all);
  // any out-of-range index is treated as std::nullopt. Trailing fields with
  // defaults so existing aggregate-initializer call sites keep compiling.
  std::vector<std::vector<std::optional<bool>>> contourEdgeOverrides{};
  std::vector<std::vector<std::optional<bool>>> contourEdgeVisibleOverrides{};
  ChipGenerationParameters chipParameters{};
  // False for structural fold Primitives, such as PrefabField Replace
  // squares, which affect solidity but never own generated surface properties.
  bool contributesProperties{true};
  std::vector<std::vector<std::optional<WallNormalMapOverride>>>
      contourEdgeNormalMapOverrides{};
  std::vector<std::vector<std::optional<WallMaskOverride>>>
      contourEdgeWallMaskOverrides{};
  // This Primitive's own raw area (Primitive::getArea()), independent of the
  // fold - see ComputeUndistributedLiquidDepths, which is the only consumer.
  double rawArea{0};
  // Authored emitters already resolved into world-plane positions while the
  // live Primitive is snapshotted. Capture later decides survival and height.
  std::vector<ArrangementAudioEmitter> audioEmitters{};
};

struct ArrangementEdge {
  uint32_t v[2];
  // Left and right faces relative to v[0] -> v[1].
  uint32_t face[2];
  // Wall collision/visibility overrides, propagated from the PSLG Edge that
  // produced this arrangement edge.
  std::optional<bool> collidesOverride;
  std::optional<bool> visibleOverride;
  std::optional<WallNormalMapOverride> normalMapOverride;
  std::optional<WallMaskOverride> wallMaskOverride;
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
  // Operation of the Primitive selected into paletteIndex. Empty faces retain
  // this so a Difference can own the Border walls created by its cut.
  Primitive::Operation operation{Primitive::Operation::Union};
  bool contributesProperties{false};
  // Provenance of the final solid through the ordered fold. Unlike raw
  // membership, this is cleared when a later Difference/Intersection/XOR
  // removes the accumulated solid and is retained when a Union overpaints it.
  // Trailing so existing aggregate fixtures retain their field order.
  Membership solidContributors{0};
};

// One direct liquid-adjacency between two solid Arrangement faces (the same
// faces BuildArrangementTriangles renders and the player walks): either they
// share a wall whose vertical clearance (the same headroom computation
// BuildArrangementWalls uses for player movement) is nonzero, or one side is
// the Arrangement's unbounded exterior face (index 0) and the Border wall
// between them is explicitly authored not to collide - a solid wall there
// blocks liquid exactly as it blocks the player, so an ordinary outer wall
// is not an opening just because nothing is authored beyond it. Where it is
// open, drain is true and the exterior acts as a permanent drain with an
// effectively negative-infinite floor rather than an ordinary
// clearance-limited neighbor.
struct LiquidAdjacency {
  uint32_t face0;
  uint32_t face1;
  bool drain{false};
};

struct ArrangementResult {
  std::vector<FixedPointVertex> vertices;
  std::vector<ArrangementEdge> edges;
  std::vector<ArrangementFace> faces;
  std::vector<PrimitivePropertySet> palette;
  // Parallel to palette: the wall Sub-material's dimensions resolved on the
  // calling thread before arrangement construction reaches a worker.
  std::vector<ChipGenerationParameters> chipParametersPalette;
  // Indexed directly by primitiveIndex (unlike palette, which is offset by
  // one for the unused placeholder entry) - one entry per source Primitive,
  // for ComputeUndistributedLiquidDepths to walk a face's membership bitset
  // with.
  std::vector<Primitive::Operation> primitiveOperations;
  std::vector<double> primitiveRawAreas;
  std::vector<ArrangementAudioEmitter> audioEmitters;
};

// A wall's front face is the one its outward normal points away from: the
// solid side for Border, the lower side for FloorStep, the higher side for
// CeilingStep. v0/v1 are the wall's endpoints ordered so that walking from
// v0 to v1 keeps the front face on the left, matching normal. bottomZ/topZ
// follow that same oriented endpoint order.
struct ArrangementWallOrientation {
  wp::Vector2 v0;
  wp::Vector2 v1;
  wp::Vector2 normal;
  std::array<float, 2> bottomZ{};
  std::array<float, 2> topZ{};
};

struct ArrangementWallSurfaceVertex {
  wp::Vector2 position;
  float elevation{};
  uint8_t endpoint{};
  bool topBoundary{};
};

// The nondegenerate perimeter of one derived wall segment, ordered about its
// canonical front normal. A quadrilateral has four vertices; a segment whose
// adjacent planes meet at one endpoint is a triangle with three. Consumers
// triangulate it as a fan so rendering, picking, outlines, and acoustics use
// exactly the same surface.
struct ArrangementWallSurface {
  std::array<ArrangementWallSurfaceVertex, 4> vertices{};
  uint8_t vertexCount{};
};

using ArrangementResultPtr = std::shared_ptr<ArrangementResult const>;
using PrimitiveFoldOrder = std::vector<uint32_t>;

// Orders primitive-list indices by ascending generated priority. stable_sort
// preserves source order for equal priorities (ADR-0026).
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

// The face's outer boundary area minus the area of each of its inner
// boundaries (holes), in world units. Zero for the unbounded exterior face.
[[nodiscard]] double FaceArea(
    ArrangementFace const& face, ArrangementResult const& arrangement);

[[nodiscard]] std::vector<ArrangementTriangle> BuildArrangementTriangles(
    ArrangementResult const& arrangement);

// Converts generated Arrangement triangles into Hydraulic cells without
// changing their triangulation or exact planar topology.
[[nodiscard]] std::vector<HydraulicCell> BuildHydraulicCells(
    ArrangementResult const& arrangement,
    std::vector<ArrangementTriangle> const& triangles);

[[nodiscard]] std::vector<ArrangementWall> BuildArrangementWalls(
    ArrangementResult const& arrangement);

// The liquid-adjacency relation over every pair of solid faces, one entry
// per unordered pair, for the later watershed equilibrium pass to consume.
// This computes no liquid depth itself.
[[nodiscard]] std::vector<LiquidAdjacency> BuildLiquidAdjacency(
    ArrangementResult const& arrangement);

// Each solid face's undistributed liquid depth: the sum, over every
// Union-operation Primitive in that face's membership, of
// primitiveLiquidLevel * primitiveRawArea / primitiveSurvivingArea - that
// Primitive's whole authored volume spread at one uniform depth over however
// much of its footprint survived the fold. Volume is conserved exactly:
// subdividing a Primitive across several faces seeds every piece at the same
// depth, and carving part of it away leaves the rest correspondingly deeper.
// Deliberately uncapped by the face's own clearance, and modelling no flow
// between faces - this is the seed volume ComputeLiquidLevels then settles.
// Parallel to arrangement.faces; non-solid faces (and the unbounded exterior
// face) are always zero.
[[nodiscard]] std::vector<float> ComputeUndistributedLiquidDepths(
    ArrangementResult const& arrangement);

// Settles authored volume into horizontal Pools over Hydraulic cells. Capacity
// is integrated over each affine floor and ceiling and Pool elevation is found
// by deterministic fixed-iteration bisection, so no linear-capacity assumption
// remains. This pass retains the existing face-level adjacency/Sill graph;
// sloped links between distinct basins are generalized separately.
[[nodiscard]] LiquidState ComputeLiquidState(
    ArrangementResult const& arrangement,
    std::vector<ArrangementTriangle> const& triangles);

// Flat-world compatibility view: one settled depth per Arrangement face.
// New position-dependent consumers use LiquidState through ArrangementWorldData.
[[nodiscard]] std::vector<float> ComputeLiquidLevels(
    ArrangementResult const& arrangement);

[[nodiscard]] ArrangementWallOrientation OrientArrangementWall(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall);

[[nodiscard]] ArrangementWallSurface BuildArrangementWallSurface(
    ArrangementResult const& arrangement,
    ArrangementWall const& wall);

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
