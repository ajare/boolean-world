#pragma once

#include <willpower/common/Vector2.h>

namespace applib {

struct Beam {
  static int msBeamIdGen;

public:
  int id;
  int type;
  float time{0.0f};
};

}  // namespace applib