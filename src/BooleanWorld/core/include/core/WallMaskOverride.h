#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/Platform.h"

namespace bw::core {

// Versioned authored value carried by one External Mesh edge, sibling to the
// Wall normal-map override. Inactive states deliberately have no image
// payload, preventing stale paths, channels, or blend values from surviving a
// switch to Unset/Disabled.
class BW_API WallMaskOverride {
public:
  enum struct State : uint8_t { Unset,
                                Disabled,
                                Image };

  // The R/G/B/A channel of the mask image whose single value drives the
  // blend. Red = 0, Green = 1, Blue = 2, Alpha = 3.
  enum struct Channel : uint8_t { Red = 0,
                                  Green = 1,
                                  Blue = 2,
                                  Alpha = 3 };

  static constexpr size_t BlendParameterCount = 8;
  using BlendParameters = std::array<float, BlendParameterCount>;

  struct ImageData {
    std::string resourceName;
    uint8_t channel{0};
    BlendParameters blendParameters{};
    bool operator==(ImageData const&) const = default;
  };

  WallMaskOverride() = default;

  [[nodiscard]] static WallMaskOverride unset() { return {}; }
  [[nodiscard]] static WallMaskOverride disabled() {
    return WallMaskOverride(State::Disabled, std::nullopt);
  }
  [[nodiscard]] static WallMaskOverride image(
      std::string resourceName, uint8_t channel,
      BlendParameters blendParameters) {
    auto finite = std::all_of(
        blendParameters.begin(), blendParameters.end(),
        [](float value) { return std::isfinite(value); });
    if (resourceName.empty() || channel > 3 || !finite) {
      throw std::invalid_argument("Invalid Wall mask Image override.");
    }
    return WallMaskOverride(
        State::Image,
        ImageData{std::move(resourceName), channel, blendParameters});
  }

  [[nodiscard]] State state() const noexcept { return mState; }
  [[nodiscard]] ImageData const* imageData() const noexcept {
    return mImage ? &*mImage : nullptr;
  }
  [[nodiscard]] bool operator==(WallMaskOverride const&) const = default;

private:
  State mState{State::Unset};
  std::optional<ImageData> mImage;

  WallMaskOverride(State state, std::optional<ImageData> image)
      : mState(state), mImage(std::move(image)) {}
};

}  // namespace bw::core
