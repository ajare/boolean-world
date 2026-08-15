#pragma once

#include "Platform.h"

namespace applib {

struct BeamEmitter {
  int id;  // id of emitted beam
  wp::Vector2 anchorPos;
  float anchorAngle;
};

}  // namespace applib