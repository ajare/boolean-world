#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <core/ArrangementWorldData.h>

namespace editor {

enum class PreviewSurface { None, Floor, Ceiling, Wall };

struct PreviewSurfaceHit {
  PreviewSurface surface{PreviewSurface::None};
  // Index into ArrangementWorldData::getWalls(); only meaningful for Wall.
  size_t wallIndex{};
  // Distance from the ray origin, in world units.
  float distance{};

  [[nodiscard]] bool hit() const {
    return surface != PreviewSurface::None;
  }
};

struct PreviewScenePick {
  // Index into ArrangementWorldData::getTriangles(); only meaningful for a
  // floor or ceiling hit. The legacy name is retained for call-site churn.
  size_t primitiveIndex{};
  PreviewSurfaceHit surfaceHit;

  [[nodiscard]] bool hit() const {
    return surfaceHit.hit();
  }
};

// Finds the nearest rendered Arrangement surface along a ray, for "what is
// the player looking at" in the 3D preview. The input is the same resolved,
// composited geometry WorldRenderer draws; no Primitive ordering or
// draw-order tie breaking is involved. Coordinates are (x, y ground-plane,
// z height), and the function is deliberately free of the graphics API.
// Surfaces are two-sided, matching the rendered preview.
[[nodiscard]] PreviewScenePick pickPreviewSceneSurface(
    bw::core::ArrangementWorldData const& worldData,
    std::array<float, 3> const& rayOrigin,
    std::array<float, 3> const& rayDirection);

// A picked surface resolved back to the Arrangement polygon it belongs to,
// and to the authored Primitive whose properties that polygon was given.
//
// Every rendered surface comes from a polygon of the boolean Arrangement, and
// a polygon carries exactly one property set: the one contributed by the
// Primitive that won the fold there (ADR-0026), which is where the floor,
// ceiling and wall material ids it draws with come from. So "which Primitive
// does editing this surface edit" always has one answer, however many
// Primitives overlap the polygon - the winner, and no other.
struct PreviewSurfaceOwner {
  // Index into ArrangementResult::faces.
  uint32_t faceIndex{~0u};
  // That polygon's entry in ArrangementResult::palette.
  uint16_t paletteIndex{0};
  // Where the owner sits in the Primitive list the Arrangement was built
  // from, which is what identifies it. ~0u when the polygon has no member
  // Primitive at all, which only the unbounded exterior face has.
  //
  // Deliberately not ArrangementFace::primitiveIndex, which is the owner's
  // Primitive::getId(): an id is a Primitive's index within its own Layer
  // (Layer::_appendBuiltPrimitive), so in a World of several Layers the same
  // id names one Primitive per Layer and cannot pick between them. The
  // palette index can: it counts the Arrangement's own input list.
  uint32_t primitiveListIndex{~0u};

  [[nodiscard]] bool valid() const {
    return faceIndex != ~0u;
  }
};

// Resolves a pick from pickPreviewSceneSurface back to its owning polygon.
// A floor or ceiling names its polygon through the triangle that was hit; a
// wall names an edge, and belongs to the polygon on the side that gave it its
// height and its material - the solid side of a border, the lower side of a
// floor step, the higher side of a ceiling step, exactly as
// BuildArrangementWalls chose them.
[[nodiscard]] PreviewSurfaceOwner resolvePreviewSurfaceOwner(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick);

// The Sub-material id the picked surface currently draws with: the owning
// polygon's floor, ceiling or wall id, whichever the pick names.
[[nodiscard]] std::string previewSurfaceSubMaterialId(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& pick);

// How the surface reads in the editor: "Floor", "Ceiling" or "Wall".
[[nodiscard]] std::string_view previewSurfaceName(PreviewSurface surface);

}  // namespace editor
