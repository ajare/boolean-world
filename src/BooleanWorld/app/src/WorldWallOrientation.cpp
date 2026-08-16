#include "WorldWallOrientation.h"

#include <stdexcept>
#include <utility>

namespace bw::app {

ArrangementWallOrientation orientArrangementWall(
    core::arr::ArrangementResult const& arrangement,
    core::arr::ArrangementWall const& wall) {
  auto const& edge = arrangement.edges[wall.edge];
  auto const& face0 = arrangement.faces[edge.face[0]];
  auto const& face1 = arrangement.faces[edge.face[1]];
  auto const& fixed0 = arrangement.vertices[edge.v[0]];
  auto const& fixed1 = arrangement.vertices[edge.v[1]];

  ArrangementWallOrientation result{
      {core::arr::ToWorldCoordinate(fixed0.x),
       core::arr::ToWorldCoordinate(fixed0.y)},
      {core::arr::ToWorldCoordinate(fixed1.x),
       core::arr::ToWorldCoordinate(fixed1.y)},
      {}};
  result.normal = (result.v1 - result.v0).normalisedCopy().perpendicular();

  bool face0IsFront = false;
  switch (wall.kind) {
    case core::arr::ArrangementWallKind::Border:
      face0IsFront = !face0.solid;
      break;

    case core::arr::ArrangementWallKind::FloorStep: {
      auto const& properties0 = arrangement.palette[face0.paletteIndex];
      auto const& properties1 = arrangement.palette[face1.paletteIndex];
      face0IsFront = properties0.floorZ < properties1.floorZ;
      break;
    }

    case core::arr::ArrangementWallKind::CeilingStep: {
      auto const& properties0 = arrangement.palette[face0.paletteIndex];
      auto const& properties1 = arrangement.palette[face1.paletteIndex];
      face0IsFront = properties0.ceilingZ > properties1.ceilingZ;
      break;
    }

    default:
      throw std::logic_error("Unknown arrangement wall kind");
  }

  if (!face0IsFront) {
    result.normal = -result.normal;
    std::swap(result.v0, result.v1);
  }
  return result;
}

}  // namespace bw::app
