#pragma once

#include <cmath>
#include <cstdint>

namespace bw::core {

inline constexpr uint32_t MaximumWedgeQuality = 3;

// World-owned controls for additive post-fold Wedges (ADR-0031). Floor and
// ceiling frequencies are average Edge Wedge counts per world-unit Arris
// distance; Corner probability applies independently to each floor/ceiling
// Corner candidate. Quality is the number of recursive centroid subdivisions;
// zero preserves the six-facet Edge and one-facet Corner geometry. Reach is
// the total extent along a Border wall's floor or
// ceiling Arris; width is half-reach. Drop-down height is the serialized name
// of the vertical extent, mirrored upward for floor Wedges. Corner reach is measured independently
// away from a Corner along each incident wall Arris; Corner vertical extent
// runs down or up their shared vertical Arris.
struct WedgeGenerationParameters {
  bool enabled{false};
  float minimumReach{12.0f};
  float maximumReach{24.0f};
  float minimumDropDownHeight{6.0f};
  float maximumDropDownHeight{12.0f};
  float minimumProjectionDepth{6.0f};
  float maximumProjectionDepth{12.0f};
  float minimumCornerReach{6.0f};
  float maximumCornerReach{12.0f};
  float minimumCornerVerticalExtent{6.0f};
  float maximumCornerVerticalExtent{12.0f};
  float floorWedgesPerUnitDistance{0.05f};
  float ceilingWedgesPerUnitDistance{0.05f};
  float cornerWedgeProbability{1.0f};
  uint32_t quality{0};

  bool operator==(WedgeGenerationParameters const&) const = default;
};

[[nodiscard]] inline bool WedgeGenerationParametersAreValid(
    WedgeGenerationParameters const& parameters) {
  auto validRange = [](float minimum, float maximum) {
    return std::isfinite(minimum) && std::isfinite(maximum) &&
           minimum > 0.0f && minimum <= maximum;
  };
  auto validFrequency = [](float frequency) {
    return std::isfinite(frequency) && frequency >= 0.0f && frequency <= 1.0f;
  };
  return parameters.quality <= MaximumWedgeQuality &&
         validFrequency(parameters.floorWedgesPerUnitDistance) &&
         validFrequency(parameters.ceilingWedgesPerUnitDistance) &&
         std::isfinite(parameters.cornerWedgeProbability) &&
         parameters.cornerWedgeProbability >= 0.0f &&
         parameters.cornerWedgeProbability <= 1.0f &&
         validRange(parameters.minimumReach, parameters.maximumReach) &&
         validRange(
             parameters.minimumDropDownHeight,
             parameters.maximumDropDownHeight) &&
         validRange(
             parameters.minimumProjectionDepth,
             parameters.maximumProjectionDepth) &&
         validRange(
             parameters.minimumCornerReach,
             parameters.maximumCornerReach) &&
         validRange(
             parameters.minimumCornerVerticalExtent,
             parameters.maximumCornerVerticalExtent);
}

}  // namespace bw::core
