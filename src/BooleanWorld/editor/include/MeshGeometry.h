#pragma once

#include <cstdint>
#include <vector>

#include <willpower/common/Vector2.h>
#include <willpower/geometry/Mesh.h>

#include "core/MeshPrimitive.h"

namespace editor::meshGeometry {

[[nodiscard]] bool pointInsideRing(
    wp::geometry::Mesh const& mesh,
    wp::geometry::Polygon const& ring,
    wp::Vector2 const& point);
[[nodiscard]] float twiceSignedArea(std::vector<wp::Vector2> const& points);
[[nodiscard]] float ringArea(
    wp::geometry::Mesh const& mesh, uint32_t polygonIndex);
[[nodiscard]] uint32_t innermostRingAt(
    wp::geometry::Mesh const& mesh,
    std::vector<bw::core::MeshPrimitiveEditingProxy::NodeMapping> const& mappings,
    wp::Vector2 const& position);
[[nodiscard]] float orientation(
    wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& c);
[[nodiscard]] bool pointOnSegment(
    wp::Vector2 const& a, wp::Vector2 const& b, wp::Vector2 const& point);
[[nodiscard]] bool segmentsIntersect(
    wp::Vector2 const& a, wp::Vector2 const& b,
    wp::Vector2 const& c, wp::Vector2 const& d);
[[nodiscard]] bool properSegmentsIntersect(
    wp::Vector2 const& a, wp::Vector2 const& b,
    wp::Vector2 const& c, wp::Vector2 const& d);
[[nodiscard]] bool segmentProperlyCrossesMesh(
    wp::geometry::Mesh const& mesh,
    wp::Vector2 const& first, wp::Vector2 const& second);
uint32_t addDrawnRing(
    wp::geometry::Mesh& mesh, std::vector<wp::Vector2> const& points);

}  // namespace editor::meshGeometry
