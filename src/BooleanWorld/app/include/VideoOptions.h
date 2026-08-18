#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace bw::app {

// The fraction of screen resolution the 3d world is drawn at (ADR 0012). The
// world is composited across the whole screen whichever member is active, so a
// smaller scale buys fragment cost at the price of world detail and leaves the
// interface at native resolution either way.
enum class RenderScale : int {
  Full = 0,
  Half = 1,
  Quarter = 2,
};

// Every member, in configuration order. Callers that need one target, log line
// or menu entry per scale iterate this rather than counting members themselves.
inline constexpr std::array<RenderScale, 3> allRenderScales{
    RenderScale::Full,
    RenderScale::Half,
    RenderScale::Quarter};

inline constexpr std::size_t renderScaleCount = allRenderScales.size();

inline constexpr int renderScaleCode(RenderScale scale) {
  return static_cast<int>(scale);
}

inline constexpr std::optional<RenderScale> renderScaleFromCode(int code) {
  if (code < 0 || static_cast<std::size_t>(code) >= renderScaleCount) {
    return std::nullopt;
  }

  return allRenderScales[static_cast<std::size_t>(code)];
}

inline constexpr std::size_t renderScaleIndex(RenderScale scale) {
  return static_cast<std::size_t>(renderScaleCode(scale));
}

// Video settings the launcher hands to the game DLL at load time. The scale is
// the only one so far; it seeds the model's active scale on DLL entry.
struct VideoOptions {
  RenderScale renderScale{RenderScale::Full};
};

// The configuration's spelling of a scale, and the reverse. Both directions run
// off the same table, so a member cannot be named in one direction only.
inline constexpr std::string_view renderScaleName(RenderScale scale) {
  switch (scale) {
    case RenderScale::Half:
      return "half";
    case RenderScale::Quarter:
      return "quarter";
    case RenderScale::Full:
      break;
  }

  return "full";
}

// Matches exactly the names above and nothing else. Case folding belongs to the
// caller reading the configuration, not here.
inline constexpr std::optional<RenderScale> renderScaleFromName(std::string_view name) {
  for (auto scale : allRenderScales) {
    if (renderScaleName(scale) == name) {
      return scale;
    }
  }

  return std::nullopt;
}

// Dimensions of the target the world is drawn into at a scale, given the screen
// it is composited across.
struct RenderTargetSize {
  std::size_t width;
  std::size_t height;
};

// Sub-full sizes round up, so a target can exceed the screen by a pixel rather
// than leave one uncovered - and so no screen small enough to divide away is
// asked for a zero-sized target.
inline constexpr RenderTargetSize renderTargetSize(std::size_t screenWidth, std::size_t screenHeight, RenderScale scale) {
  std::size_t divisor = 1;

  switch (scale) {
    case RenderScale::Half:
      divisor = 2;
      break;
    case RenderScale::Quarter:
      divisor = 4;
      break;
    case RenderScale::Full:
      break;
  }

  return {
      (screenWidth + divisor - 1) / divisor,
      (screenHeight + divisor - 1) / divisor};
}

}  // namespace bw::app
