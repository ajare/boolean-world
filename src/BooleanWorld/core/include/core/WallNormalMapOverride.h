#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/Platform.h"

namespace bw::core {

// Versioned authored value carried by one External Mesh edge. Inactive states
// deliberately have no image payload, preventing stale paths or tuning values
// from surviving a switch to Unset/Disabled.
class BW_API WallNormalMapOverride {
public:
  enum struct State : uint8_t { Unset,
                                Disabled,
                                Image };
  struct ImageData {
    std::string resourceName;
    float repeat{1.0f};
    float strength{1.0f};
    bool operator==(ImageData const&) const = default;
  };

  WallNormalMapOverride() = default;

  [[nodiscard]] static WallNormalMapOverride unset() { return {}; }
  [[nodiscard]] static WallNormalMapOverride disabled() {
    return WallNormalMapOverride(State::Disabled, std::nullopt);
  }
  [[nodiscard]] static WallNormalMapOverride image(
      std::string resourceName, float repeat, float strength) {
    if (resourceName.empty() || !std::isfinite(repeat) ||
        repeat < 0.01f || repeat > 4096.0f ||
        !std::isfinite(strength) || strength < 0.0f || strength > 2.0f) {
      throw std::invalid_argument("Invalid wall normal-map Image override.");
    }
    return WallNormalMapOverride(
        State::Image,
        ImageData{std::move(resourceName), repeat, strength});
  }

  [[nodiscard]] State state() const noexcept { return mState; }
  [[nodiscard]] ImageData const* imageData() const noexcept {
    return mImage ? &*mImage : nullptr;
  }
  [[nodiscard]] bool operator==(WallNormalMapOverride const&) const = default;

private:
  State mState{State::Unset};
  std::optional<ImageData> mImage;

  WallNormalMapOverride(State state, std::optional<ImageData> image)
      : mState(state), mImage(std::move(image)) {}
};

}  // namespace bw::core
