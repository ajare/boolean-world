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

// The authored rectangular opening requested by one Portal endpoint. The
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
  friend class Layer;
public:
  Portal(uint32_t id, std::string name, AuthoredAperture aperture,
         uint32_t targetId);
  [[nodiscard]] uint32_t getId() const { return mId; }
  [[nodiscard]] std::string const& getName() const { return mName; }
  [[nodiscard]] AuthoredAperture const& getAperture() const { return mAperture; }
  [[nodiscard]] uint32_t getTargetId() const { return mTargetId; }
};

// No authored loop owns an independent Portal. In transitional consumer keys,
// this reserved loop ID means endpointId is a Layer-local Portal ID.
inline constexpr uint32_t IndependentPortalLoopId = ~0u;

class BW_API PortalEndpoint {
  uint32_t mId{};
  AuthoredAperture mAperture{};

public:
  PortalEndpoint() = default;
  PortalEndpoint(uint32_t id, AuthoredAperture aperture);

  [[nodiscard]] uint32_t getId() const;
  [[nodiscard]] AuthoredAperture const& getAperture() const;

private:
  friend class PortalLoop;
  friend class Layer;
  void setAperture(AuthoredAperture const& aperture);
};

// A permanently Layer-owned ordered cycle of stable endpoint identities.
// Endpoint storage is independent of its explicit directed traversal order.
class BW_API PortalLoop {
  uint32_t mId{};
  uint32_t mNextEndpointId{2};
  std::vector<PortalEndpoint> mEndpoints{
      PortalEndpoint{0, {}}, PortalEndpoint{1, {}}};
  // Stable endpoint IDs in directed traversal order. Keeping this separate
  // from storage prevents authored identity from becoming a container index.
  std::vector<uint32_t> mTraversalOrder{0, 1};

public:
  PortalLoop() = default;
  PortalLoop(
      uint32_t id, AuthoredAperture first, AuthoredAperture second);
  PortalLoop(
      uint32_t id, uint32_t nextEndpointId,
      std::vector<PortalEndpoint> endpoints,
      std::vector<uint32_t> traversalOrder);

  [[nodiscard]] uint32_t getId() const;
  [[nodiscard]] uint32_t getNextEndpointAllocator() const;
  [[nodiscard]] std::vector<PortalEndpoint> const& getEndpoints() const;
  [[nodiscard]] PortalEndpoint const* findEndpoint(uint32_t endpointId) const;
  [[nodiscard]] std::span<uint32_t const> getTraversalOrder() const;
  [[nodiscard]] uint32_t getNextEndpointId(uint32_t endpointId) const;

private:
  friend class Layer;
  [[nodiscard]] PortalEndpoint* findEndpointMutable(uint32_t endpointId);
  [[nodiscard]] uint32_t addEndpointAfter(
      uint32_t afterEndpointId, AuthoredAperture const& aperture);
  void removeEndpoint(uint32_t endpointId);
  bool moveEndpointEarlier(uint32_t endpointId);
  bool moveEndpointLater(uint32_t endpointId);
};

// First reason an authored endpoint or its loop cannot participate in this
// generation. Diagnostics are snapshot data, not authored state.
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
  uint32_t endpointId{};
  AuthoredAperture authored{};
  bool resolved{false};
  PortalResolutionDiagnostic diagnostic{
      PortalResolutionDiagnostic::MissingRenderedWall};
  ResolvedAperture aperture{};
};

struct ResolvedPortalLoop {
  uint32_t layerId{};
  // Independent named cycles use IndependentPortalLoopId here and the
  // authored Layer-local Portal ID in endpointId/traversalOrder. The first
  // traversal ID is the smallest member and identifies the generated cycle.
  uint32_t loopId{};
  bool active{false};
  PortalResolutionDiagnostic diagnostic{PortalResolutionDiagnostic::None};
  std::vector<ResolvedPortalEndpoint> endpoints;
  std::vector<uint32_t> traversalOrder;
};

// Canonical directed routing seam. Order contains stable endpoint IDs, not
// vector indices; missing sources and empty cycles are invalid.
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
  uint32_t loopId{};
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
  uint32_t loopId{};
  PortalLiquidDiagnostic diagnostic{
      PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint};
  // Smallest member ID distinguishes independent cycles sharing the reserved
  // loop namespace; absent for legacy authored loops.
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
struct PortalLoopSnapshot {
  uint32_t layerId{};
  PortalLoop loop{};
  // When present, this independently authored Portal replaces `loop` as the
  // resolution input. Legacy authored loops remain unchanged during expansion.
  std::optional<Portal> portal;
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
    std::vector<PortalLoopSnapshot> const& loops);

// Resolves active apertures onto their incident Hydraulic cells, then accepts
// Portal loops in stable (Layer id, loop id, cycle-start Portal id) order.
// Ordinary links participate
// in the offset graph. A loop that would close a contradictory elevation cycle
// is diagnosed and omitted atomically.
[[nodiscard]] BW_API PortalLiquidAdjacencyResult
BuildPortalLiquidAdjacency(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<arr::HydraulicCell> const& cells,
    std::vector<ResolvedPortalLoop> const& loops);

}  // namespace bw::core
