#pragma once

#include <cstdint>

enum class FloorPattern : int32_t {
  None = 0,
  Square = 1,
  Hexagon = 2,
  RunningBond = 3,
  ModularOpus = 4,
  Voronoi = 5,
};

struct FloorPatternOptions {
  FloorPattern pattern{FloorPattern::Hexagon};
  float radius{16.0f};
  float depth{0.5f};
  float tileDepthVariationFactor{0.1f};
  float runningBondWidthPercent{50.0f};
  float runningBondOffsetPercent{50.0f};
  float voronoiRoundedEdgeFactor{0.25f};

  int32_t secondaryMaterialIndex{-1};
  bool usesSecondaryMaterial{false};
};
