#include "core/LiquidProperties.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bw {
namespace core {

using namespace std;

namespace {

// Indexed by LiquidType, so entries stay in the enum's order.
constexpr array<LiquidProperties, LiquidTypeCount> liquidProperties{{
    // Water is the reference liquid, so its density is 1 by definition and
    // BW_PLAYER_DENSITY is written relative to it. The viscosity is tuned
    // against the player: it leaves an unmoving swimmer drifting back up to
    // the surface at a wallow rather than a bob, while still letting a fall
    // from a height drive them well under, and gives a swimming player about
    // 30 units per second of vertical speed while fully submerged. Its optical
    // values preserve the former liquid-material albedo and approximately its
    // apparent opacity over a typical 40-unit pool depth.
    {1.0f, 12.0f, 0.55f, 40.0f, {0.10f, 0.35f, 0.60f}},
}};

}  // namespace

LiquidProperties const& GetLiquidProperties(LiquidType type) {
  auto index = static_cast<int32_t>(type);
  if (index < 0 || index >= LiquidTypeCount) {
    return liquidProperties[0];
  }
  return liquidProperties[static_cast<size_t>(index)];
}

array<float, 3> CalculateLiquidExtinction(
    LiquidProperties const& properties) {
  // A reference depth at or below zero cannot express an attenuation distance.
  // Treat it as the smallest positive depth rather than divide by zero.
  constexpr float minimumReferenceDepth = 0.001f;
  constexpr float maximumOpacity = 0.999f;
  auto opacity = clamp(properties.opacity, 0.0f, maximumOpacity);
  auto baseExtinction = -log(1.0f - opacity) /
                        max(properties.referenceDepth, minimumReferenceDepth);

  array<float, 3> extinction;
  for (size_t channel = 0; channel < extinction.size(); ++channel) {
    // A neutral midpoint tint leaves the scalar authored opacity unchanged.
    // Brighter tint channels are retained more readily, while darker channels
    // are absorbed more strongly.
    extinction[channel] = baseExtinction *
                          (2.0f * (1.0f - clamp(properties.tint[channel],
                                                 0.0f, 1.0f)));
  }
  return extinction;
}

float CalculateLiquidPathLength(
    std::array<float, 3> const& eyePosition,
    std::array<float, 3> const& pointPosition,
    float eyeSurfaceHeight,
    float pointSurfaceHeight) {
  auto eyeIsWet = eyeSurfaceHeight > eyePosition[1];
  auto pointIsWet = pointSurfaceHeight > pointPosition[1];
  auto surfaceHeight = eyeIsWet ? eyeSurfaceHeight : pointSurfaceHeight;

  auto low = std::min(eyePosition[1], pointPosition[1]);
  auto high = std::max(eyePosition[1], pointPosition[1]);
  auto verticalSpan = high - low;
  auto submergedFraction = verticalSpan > 0.001f
                               ? std::clamp((std::min(high, surfaceHeight) - low) /
                                                verticalSpan,
                                            0.0f, 1.0f)
                               : (0.5f * (low + high) <= surfaceHeight ? 1.0f
                                                                       : 0.0f);
  auto deltaX = pointPosition[0] - eyePosition[0];
  auto deltaY = pointPosition[1] - eyePosition[1];
  auto deltaZ = pointPosition[2] - eyePosition[2];
  auto chordLength = std::sqrt(
      deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

  if (pointIsWet) {
    return chordLength * submergedFraction;
  }
  return std::max(surfaceHeight - eyePosition[1], 0.0f);
}

}  // namespace core
}  // namespace bw
