#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
    std::string resourcePath;
    float unitsPerRepeat{64.0f};
    float strength{1.0f};
    bool operator==(ImageData const&) const = default;
  };

  WallNormalMapOverride() = default;

  [[nodiscard]] static WallNormalMapOverride unset() { return {}; }
  [[nodiscard]] static WallNormalMapOverride disabled() {
    return WallNormalMapOverride(State::Disabled, std::nullopt);
  }
  [[nodiscard]] static WallNormalMapOverride image(
      std::string resourcePath, float unitsPerRepeat, float strength) {
    std::filesystem::path path(resourcePath);
    auto normalized = path.lexically_normal();
    auto escapesResourceRoot = std::ranges::any_of(
        normalized, [](auto const& component) { return component == ".."; });
    if (resourcePath.empty() || path.is_absolute() || normalized.empty() ||
        escapesResourceRoot || !std::isfinite(unitsPerRepeat) ||
        unitsPerRepeat < 0.01f || unitsPerRepeat > 4096.0f ||
        !std::isfinite(strength) || strength < 0.0f || strength > 2.0f) {
      throw std::invalid_argument("Invalid wall normal-map Image override.");
    }
    return WallNormalMapOverride(
        State::Image,
        ImageData{normalized.generic_string(), unitsPerRepeat, strength});
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
