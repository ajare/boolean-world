#pragma once

#include "Beam.h"
#include "BeamData.h"

namespace applib {
struct Entity;

struct BeamInstance {
  Beam beam;
  BeamData data;
  Entity* owner;
  bool alive;
};

}  // namespace applib