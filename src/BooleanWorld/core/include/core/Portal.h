#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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

class BW_API PortalEndpoint {
  uint32_t mId{};
  AuthoredAperture mAperture{};

public:
  PortalEndpoint() = default;
  PortalEndpoint(uint32_t id, AuthoredAperture aperture);

  [[nodiscard]] uint32_t getId() const;
  [[nodiscard]] AuthoredAperture const& getAperture() const;

private:
  friend class PortalPair;
  friend class Layer;
  void setAperture(AuthoredAperture const& aperture);
};

// A permanently Layer-owned link with exactly two stable endpoint identities.
// Endpoint storage is independent of its explicit directed traversal order.
class BW_API PortalPair {
  uint32_t mId{};
  std::array<PortalEndpoint, 2> mEndpoints{
      PortalEndpoint{0, {}}, PortalEndpoint{1, {}}};
  // Stable endpoint IDs in directed traversal order. Keeping this separate
  // from storage prevents authored identity from becoming a container index.
  std::array<uint32_t, 2> mTraversalOrder{0, 1};

public:
  PortalPair() = default;
  PortalPair(
      uint32_t id, AuthoredAperture first, AuthoredAperture second);
  PortalPair(
      uint32_t id, std::array<PortalEndpoint, 2> endpoints,
      std::array<uint32_t, 2> traversalOrder);

  [[nodiscard]] uint32_t getId() const;
  [[nodiscard]] std::array<PortalEndpoint, 2> const& getEndpoints() const;
  [[nodiscard]] PortalEndpoint const* findEndpoint(uint32_t endpointId) const;
  [[nodiscard]] std::span<uint32_t const> getTraversalOrder() const;
  [[nodiscard]] uint32_t getNextEndpointId(uint32_t endpointId) const;

private:
  friend class Layer;
  [[nodiscard]] PortalEndpoint* findEndpointMutable(uint32_t endpointId);
};

// First reason an authored endpoint or its pair cannot participate in this
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
// traversal. A conflicting pair remains an active Portal but contributes no
// portal liquid-adjacency to this snapshot.
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
  uint8_t endpointId{};
  AuthoredAperture authored{};
  bool resolved{false};
  PortalResolutionDiagnostic diagnostic{
      PortalResolutionDiagnostic::MissingRenderedWall};
  ResolvedAperture aperture{};
};

struct ResolvedPortalPair {
  uint32_t layerId{};
  uint32_t pairId{};
  bool active{false};
  PortalResolutionDiagnostic diagnostic{PortalResolutionDiagnostic::None};
  std::array<ResolvedPortalEndpoint, 2> endpoints{};
  std::array<uint32_t, 2> traversalOrder{0, 1};
};

// Canonical directed routing seam. Order contains stable endpoint IDs, not
// vector indices; missing sources and cycles shorter than two are invalid.
[[nodiscard]] BW_API uint32_t NextPortalEndpointId(
    std::span<uint32_t const> order, uint32_t sourceId);
// Pair-facing adapter retained for render consumers during migration. The
// next position is obtained from stable traversal identity.
[[nodiscard]] BW_API uint32_t NextPortalEndpointIndex(
    ResolvedPortalPair const& pair, uint32_t sourceIndex);
// Stable-identity adapters for consumers migrating away from endpoint
// positions. The pair's current endpoint sequence is its traversal order.
[[nodiscard]] BW_API ResolvedPortalEndpoint const* FindPortalEndpoint(
    ResolvedPortalPair const& pair, uint32_t endpointId);
[[nodiscard]] BW_API ResolvedPortalEndpoint const* NextPortalEndpoint(
    ResolvedPortalPair const& pair, uint32_t sourceEndpointId);

// A generated, bidirectional connection between Hydraulic cells touching the
// two resolved apertures. This is intentionally distinct from ordinary
// shared-edge Hydraulic links and from wall collision. At equilibrium the
// destination surface is elevationOffset above the source surface; sill0 and
// sill1 are the two resolved lower edges and differ by that same offset.
struct PortalLiquidAdjacency {
  uint32_t layerId{};
  uint32_t pairId{};
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
  uint32_t pairId{};
  PortalLiquidDiagnostic diagnostic{
      PortalLiquidDiagnostic::NoHydraulicCellAtEndpoint};
};

struct PortalLiquidAdjacencyResult {
  std::vector<PortalLiquidAdjacency> adjacency;
  std::vector<PortalLiquidAdjacencyDiagnostic> diagnostics;
};

// The canonical rigid mapping from a source to its next endpoint frame. The
// local tangent and front axes are both reversed (a 180-degree turn around
// world-up); elevation is translated by the difference between aperture
// bottoms. Scale, handedness, and world-up are therefore preserved.
struct PortalRigidTransform {
  ResolvedAperture source{};
  ResolvedAperture destination{};

  [[nodiscard]] wp::Vector2 transformPoint(
      wp::Vector2 const& point) const;
  [[nodiscard]] wp::Vector2 transformVector(
      wp::Vector2 const& vector) const;
  [[nodiscard]] float transformElevation(float elevation) const;
  [[nodiscard]] float transformYaw(float yawDegrees) const;
};

[[nodiscard]] BW_API PortalRigidTransform BuildPortalRigidTransform(
    ResolvedPortalPair const& pair, uint32_t sourceEndpointId);

// A value-only copy made on the generation-requesting thread. It is safe to
// carry to the asynchronous arrangement worker with the other generation
// inputs.
struct PortalPairSnapshot {
  uint32_t layerId{};
  PortalPair pair{};
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

[[nodiscard]] BW_API std::vector<ResolvedPortalPair> ResolvePortalPairs(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<PortalPairSnapshot> const& pairs);

// Resolves active apertures onto their incident Hydraulic cells, then accepts
// Portal pairs in stable (Layer id, pair id) order. Ordinary links participate
// in the offset graph. A pair that would close a contradictory elevation cycle
// is diagnosed and omitted atomically.
[[nodiscard]] BW_API PortalLiquidAdjacencyResult
BuildPortalLiquidAdjacency(
    arr::ArrangementResult const& arrangement,
    std::vector<arr::ArrangementWall> const& walls,
    std::vector<arr::HydraulicCell> const& cells,
    std::vector<ResolvedPortalPair> const& pairs);

}  // namespace bw::core
