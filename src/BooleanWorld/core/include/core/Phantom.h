#pragma once

#include "core/Arrangement.h"

namespace bw::core {

// A hidden wall is not a surface in Phantom: its generated, unchipped outline
// bounds a one-sided view into Euclidean space. Keep rendering, picking and
// traversal eligibility identical.
inline bool isPhantomAperture(arr::ArrangementWall const& wall) {
  return !wall.visible && wall.kind == arr::ArrangementWallKind::Border &&
      wall.sideZones &&
      ((*wall.sideZones == std::array{ZoneId::Euclidean, ZoneId::Phantom}) ||
       (*wall.sideZones == std::array{ZoneId::Phantom, ZoneId::Euclidean}));
}

inline bool phantomApertureFacesEye(arr::ArrangementResult const& arrangement,
    arr::ArrangementWall const& wall, wp::Vector2 const& eye) {
  if (!isPhantomAperture(wall)) return false;
  auto frame = arr::OrientArrangementWall(arrangement, wall);
  return (eye - frame.v0).dot(frame.normal) < 0.0f;
}

} // namespace bw::core
