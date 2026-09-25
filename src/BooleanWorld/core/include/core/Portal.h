#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"

namespace bw::core {
namespace arr {
struct ArrangementResult;
struct ArrangementWall;
struct HydraulicCell;
}  // namespace arr

// The authored rectangular opening requested by one Portal. The
// centre lives in the World plane; the width follows the rendered wall found
// at generation time. Elevations are world-up bounds and are never changed by
// generation.
struct AuthoredAperture {
  wp::Vector2 centre{};
  float width{16.0f};
  float bottom{0.0f};
  float top{24.0f};
};

// Independent authored Portal with a same-Layer stable-ID target.
class BW_API Portal {
  uint32_t mId{};
  std::string mName;
  AuthoredAperture mAperture{};
  uint32_t mTargetId{};
  bool mBlocksWater{false};
  friend class Layer;
public:
  Portal(uint32_t id, std::string name, AuthoredAperture aperture,
         uint32_t targetId);
  [[nodiscard]] uint32_t getId() const { return mId; }
  [[nodiscard]] std::string const& getName() const { return mName; }
  [[nodiscard]] AuthoredAperture const& getAperture() const { return mAperture; }
  [[nodiscard]] uint32_t getTargetId() const { return mTargetId; }
  // Restricts outgoing Liquid transport only, never incoming hops.
  [[nodiscard]] bool getBlocksWater() const { return mBlocksWater; }
};

// Target-graph validity is independent of aperture resolution. Every Portal
// still has exactly one outgoing target, but an editable component is active
// only when every member has exactly one incoming reference.
enum class PortalTargetGraphDiagnostic : uint8_t {
  None,
  MissingIncomingReference,
  MultipleIncomingReferences,
  OtherPortalInvalid
};

[[nodiscard]] BW_API std::string_view PortalTargetGraphDiagnosticText(
    PortalTargetGraphDiagnostic diagnostic);

// First aperture-resolution reason a Portal or its inferred cycle cannot
// participate in this generation. Diagnostics are snapshot data, not authored
// state, and remain available even when the target graph is invalid.
enum class PortalResolutionDiagnostic : uint8_t {
  None,
  UnequalEndpointHeights,
  InsufficientPlayerWidth,
  InsufficientPlayerHeight,
  MissingRenderedWall,
  IncompleteRenderedWallCoverage,
  OtherEndpointUnresolved
};

[[nodiscard]] BW_API std::string_view PortalResolutionDiagnosticText(
    PortalResolutionDiagnostic diagnostic);

// Liquid-specific generation failures do not deactivate rendering or player
// traversal. A conflicting loop remains active but contributes no Portal
// liquid-adjacency to this snapshot.
enum class PortalLiquidDiagnostic : uint8_t {
  NoHydraulicCellAtEndpoint,
  ContradictoryElevationCycle
};

[[nodiscard]] BW_API std::string_view PortalLiquidDiagnosticText(
    PortalLiquidDiagnostic diagnostic);

// Immutable generation-side rectangle. wallIndices refer only to the owning
// ArrangementWorldData snapshot and are intentionally absent from authored
// serialization.
struct ResolvedAperture {
  wp::Vector2 centre{};
  wp::Vector2 tangent{};
  wp::Vector2 front{};
  float width{};
  float bottom{};
  float top{};
  std::vector<uint32_t> wallIndices;
};

struct ResolvedPortalEndpoint {
  // The authored Layer-local Portal ID, never an index or loop-local ID.
  uint32_t endpointId{};
  AuthoredAperture authored{};
  bool blocksWater{false};
  bool resolved{false};
  PortalTargetGraphDiagnostic targetGraphDiagnostic{
      PortalTargetGraphDiagnostic::None};
  PortalResolutionDiagnostic diagnostic{
      PortalResolutionDiagnostic::MissingRenderedWall};
  ResolvedAperture aperture{};
};

// Inferred target component, not an authored object. Active cycles follow
// targets from the smallest member ID; inactive components list members by ID.
struct ResolvedPortalLoop {
  uint32_t layerId{};
  bool active{false};
  PortalTargetGraphDiagnostic targetGraphDiagnostic{
      PortalTargetGraphDiagnostic::None};
  PortalResolutionDiagnostic diagnostic{PortalResolutionDiagnostic::None};
  std::vector<ResolvedPortalEndpoint> endpoints;
  std::vector<uint32_t> traversalOrder;
};

// Canonical directed routing seam for an active component. Order contains
// Layer-local Portal IDs, not vector indices; missing sources and empty cycles
// are invalid.
[[nodiscard]] BW_API uint32_t NextPortalEndpointId(
    std::span<uint32_t const> order, uint32_t sourceId);
// Stable-identity lookup used by every generated Portal consumer.
[[nodiscard]] BW_API ResolvedPortalEndpoint const* FindPortalEndpoint(
    ResolvedPortalLoop const& portalLoop, uint32_t endpointId);
[[nodiscard]] BW_API ResolvedPortalEndpoint const* NextPortalEndpoint(
    ResolvedPortalLoop const& portalLoop, uint32_t sourceEndpointId);

// A generated directed next-endpoint hop between incident Hydraulic cells,
// distinct from ordinary shared-edge Hydraulic links and wall collision.
// Liquid crosses only from cell0 after reaching sill0. Its surface maps by
// elevationOffset (destination bottom minus source bottom); sill1 is the
// destination lower edge. A reverse hop exists only if explicitly generated.
struct PortalLiquidAdjacency {
  uint32_t layerId{};
  uint32_t cyclePortalId{};
  uint32_t sourceEndpointId{};
  uint32_t destinationEndpointId{};
  uint32_t cell0{};
  uint32_t cell1{};
  uint32_t face0{};
  uint32_t face1{};
  double sill0{};
  double sill1{};
  double elevationOffset{};
  float resolvedWidth{};
};

struct PortalLiquidAdjacencyDiagnostic {
  uint32_t layerId{};
  PortalLiquidDiagnostic diagnostic{
      PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint};
  // Smallest Portal ID identifies this inferred component in the snapshot.
  uint32_t cyclePortalId{~0u};
};

struct PortalLiquidAdjacencyResult {
  std::vector<PortalLiquidAdjacency> adjacency;
  std::vector<PortalLiquidAdjacencyDiagnostic> diagnostics;
};

enum class PortalMappingKind : uint8_t { Hop, Reflection };

// Canonical generated isometry. Hops reverse both local axes and translate
// aperture-bottom elevation; reflections preserve tangent and elevation and
// reverse only front. Both preserve scale and World-up. No authored reflection
// or reflection liquid-adjacency is implied by this value-only contract.
struct BW_API PortalMapping {
  ResolvedAperture source{};
  ResolvedAperture destination{};
  PortalMappingKind kind{PortalMappingKind::Hop};

  [[nodiscard]] bool reversesHandedness() const;
  [[nodiscard]] double elevationOffset() const;
  // Column-major renderer coordinates (World x, elevation, -World y).
  [[nodiscard]] std::array<float, 16> rendererMatrix() const;
  [[nodiscard]] wp::Vector2 transformPoint(
      wp::Vector2 const& point) const;
  [[nodiscard]] wp::Vector2 transformVector(
      wp::Vector2 const& vector) const;
  [[nodiscard]] float transformElevation(float elevation) const;
  [[nodiscard]] float transformYaw(float yawDegrees) const;
};

[[nodiscard]] BW_API PortalMapping BuildPortalReflection(
    ResolvedAperture const& aperture);

[[nodiscard]] BW_API PortalMapping BuildPortalMapping(
    ResolvedPortalLoop const& portalLoop, uint32_t sourceEndpointId);

// A value-only copy made on the generation-requesting thread. It is safe to
// carry to the asynchronous arrangement worker with the other generation
// inputs.
struct PortalSnapshot {
  uint32_t layerId{};
  Portal portal;
};

[[nodiscard]] BW_API bool AuthoredApertureIsValid(
    AuthoredAperture const& aperture);

// Finds the nearest centre at which the requested horizontal and vertical
// aperture is completely covered by collinear rendered walls. The returned
// point is no farther than maxDistance from target. This uses the same wall
// visibility, orientation, elevation, and continuity rules as resolution.
[[nodiscard]] BW_API std::optional<wp::Vector2>
FindNearestLegalPortalCentre(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    AuthoredAperture const& aperture, float resolvedWidth,
    wp::Vector2 const& target, float maxDistance);

[[nodiscard]] BW_API std::vector<ResolvedPortalLoop> ResolvePortalLoops(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<PortalSnapshot> const& portals);

// Resolves active apertures onto their incident Hydraulic cells, then accepts
// inferred Portal loops in stable (Layer id, smallest member Portal id) order.
// Blocked outgoing hops contribute neither adjacency nor offset constraints.
// Ordinary links participate in the offset graph. If the enabled hops close a
// contradictory elevation cycle, they are diagnosed and omitted atomically.
[[nodiscard]] BW_API PortalLiquidAdjacencyResult
BuildPortalLiquidAdjacency(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<arr::HydraulicCell> const& cells,
    std::vector<ResolvedPortalLoop> const& loops);

}  // namespace bw::core
