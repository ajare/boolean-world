#pragma once

#include <cstdint>

// The debug-only "lay a second material into the pattern" option. Which tiles
// take the secondary material is decided by the surface's own Sub-material
// emboss pattern and tile size (EMBOSS_PATTERN/EMBOSS_RADIUS), so this carries
// only the choice of material - the pattern itself is authored per
// Sub-material, not set globally. Patterns with two interleaved tile classes
// (square, hexagon, modular opus) are the ones that respond to it.
struct SecondaryMaterialOptions {
  int32_t materialIndex{-1};
  bool enabled{false};
};
