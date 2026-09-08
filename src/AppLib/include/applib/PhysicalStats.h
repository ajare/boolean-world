#pragma once

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "Platform.h"

namespace applib {

struct PhysicalStats {
  wp::Vector2 position;
  // The player's feet elevation, which may differ from the sampled floor
  // while stepping, falling, or swimming.
  float feetElevation;
  float angle;
  float pitch;
  bool collides;
  wp::BoundingBox bounds;
};

}  // namespace applib