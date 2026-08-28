#pragma once

#include <cmath>

namespace bw::core {

// World-owned controls for additive post-fold Wedges (ADR-0031). Reach is
// the total extent along a Border wall's top Arris; width is half-reach.
struct WedgeGenerationParameters {
  bool enabled{false};
  float minimumReach{4.0f};
  float maximumReach{8.0f};
  float minimumDropDownHeight{2.0f};
  float maximumDropDownHeight{4.0f};
  float minimumProjectionDepth{2.0f};
  float maximumProjectionDepth{4.0f};

  bool operator==(WedgeGenerationParameters const&) const = default;
};

[[nodiscard]] inline bool WedgeGenerationParametersAreValid(
    WedgeGenerationParameters const& parameters) {
  auto validRange = [](float minimum, float maximum) {
    return std::isfinite(minimum) && std::isfinite(maximum) &&
           minimum > 0.0f && minimum <= maximum;
  };
  return validRange(parameters.minimumReach, parameters.maximumReach) &&
         validRange(
             parameters.minimumDropDownHeight,
             parameters.maximumDropDownHeight) &&
         validRange(
             parameters.minimumProjectionDepth,
             parameters.maximumProjectionDepth);
}

}  // namespace bw::core
