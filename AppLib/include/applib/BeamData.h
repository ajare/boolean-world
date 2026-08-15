#pragma once

#include <cstdint>

namespace applib {
struct BeamData {
  float width;
  float length;
  uint32_t state{0};
};

}  // namespace applib