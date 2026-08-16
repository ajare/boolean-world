#pragma once

#include <core/Arrangement.h>

#include <willpower/common/Vector2.h>

namespace bw::app {

struct ArrangementWallOrientation {
  wp::Vector2 v0;
  wp::Vector2 v1;
  wp::Vector2 normal;
};

[[nodiscard]] ArrangementWallOrientation orientArrangementWall(
    core::arr::ArrangementResult const& arrangement,
    core::arr::ArrangementWall const& wall);

}  // namespace bw::app
