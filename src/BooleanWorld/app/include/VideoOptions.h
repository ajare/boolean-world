#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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
  Eighth = 3,
};

// Every member, in configuration order. Callers that need one target, log line
// or menu entry per scale iterate this rather than counting members themselves.
inline constexpr std::array<RenderScale, 4> allRenderScales{
    RenderScale::Full,
    RenderScale::Half,
    RenderScale::Quarter,
    RenderScale::Eighth};

inline constexpr std::size_t renderScaleCount = allRenderScales.size();

// One mutually-exclusive world anti-aliasing choice. MSAA is applied during
// scene rasterization; FXAA is applied to the resolved image.
enum class AntiAliasing : int {
  Off = 0,
  Msaa2x = 1,
  Msaa4x = 2,
  Msaa8x = 3,
  Fxaa = 4,
};

inline constexpr std::array<AntiAliasing, 5> allAntiAliasingOptions{
    AntiAliasing::Off,
    AntiAliasing::Msaa2x,
    AntiAliasing::Msaa4x,
    AntiAliasing::Msaa8x,
    AntiAliasing::Fxaa};

inline constexpr std::size_t antiAliasingOptionCount =
    allAntiAliasingOptions.size();

// Sampling used when the resolved world target is stretched across the screen.
enum class RenderTextureFilter : int {
  Linear = 0,
  Nearest = 1,
};

inline constexpr std::array<RenderTextureFilter, 2> allRenderTextureFilters{
    RenderTextureFilter::Linear,
    RenderTextureFilter::Nearest};

inline constexpr std::size_t renderTextureFilterCount =
    allRenderTextureFilters.size();

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

// Video settings the launcher hands to the game DLL at load time. They seed
// the model's active world-rendering options on DLL entry.
struct VideoOptions {
  RenderScale renderScale{RenderScale::Full};
  AntiAliasing antiAliasing{AntiAliasing::Off};
  RenderTextureFilter renderTextureFilter{RenderTextureFilter::Linear};
};

// The configuration's spelling of a scale, and the reverse. Both directions run
// off the same table, so a member cannot be named in one direction only.
inline constexpr std::string_view renderScaleName(RenderScale scale) {
  switch (scale) {
    case RenderScale::Half:
      return "half";
    case RenderScale::Quarter:
      return "quarter";
    case RenderScale::Eighth:
      return "eighth";
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

inline constexpr std::string_view renderTextureFilterName(
    RenderTextureFilter filter) {
  return filter == RenderTextureFilter::Nearest ? "nearest" : "linear";
}

inline constexpr int renderTextureFilterCode(RenderTextureFilter filter) {
  return static_cast<int>(filter);
}

inline constexpr std::optional<RenderTextureFilter> renderTextureFilterFromCode(
    int code) {
  if (code < 0 || static_cast<std::size_t>(code) >= renderTextureFilterCount) {
    return std::nullopt;
  }

  return allRenderTextureFilters[static_cast<std::size_t>(code)];
}

inline constexpr std::optional<RenderTextureFilter> renderTextureFilterFromName(
    std::string_view name) {
  for (auto filter : allRenderTextureFilters) {
    if (renderTextureFilterName(filter) == name) {
      return filter;
    }
  }

  return std::nullopt;
}

inline constexpr std::string_view antiAliasingName(AntiAliasing antiAliasing) {
  switch (antiAliasing) {
    case AntiAliasing::Msaa2x:
      return "msaa-2x";
    case AntiAliasing::Msaa4x:
      return "msaa-4x";
    case AntiAliasing::Msaa8x:
      return "msaa-8x";
    case AntiAliasing::Fxaa:
      return "fxaa";
    case AntiAliasing::Off:
      break;
  }

  return "off";
}

inline constexpr std::string_view antiAliasingLabel(AntiAliasing antiAliasing) {
  switch (antiAliasing) {
    case AntiAliasing::Msaa2x:
      return "2x MSAA";
    case AntiAliasing::Msaa4x:
      return "4x MSAA";
    case AntiAliasing::Msaa8x:
      return "8x MSAA";
    case AntiAliasing::Fxaa:
      return "FXAA";
    case AntiAliasing::Off:
      break;
  }

  return "Off";
}

inline constexpr uint32_t antiAliasingMsaaSamples(
    AntiAliasing antiAliasing) {
  switch (antiAliasing) {
    case AntiAliasing::Msaa2x:
      return 2;
    case AntiAliasing::Msaa4x:
      return 4;
    case AntiAliasing::Msaa8x:
      return 8;
    case AntiAliasing::Off:
    case AntiAliasing::Fxaa:
      return 1;
  }

  return 1;
}

inline constexpr bool antiAliasingIsFxaa(AntiAliasing antiAliasing) {
  return antiAliasing == AntiAliasing::Fxaa;
}

inline constexpr int antiAliasingCode(AntiAliasing antiAliasing) {
  return static_cast<int>(antiAliasing);
}

inline constexpr std::optional<AntiAliasing> antiAliasingFromCode(int code) {
  if (code < 0 || static_cast<std::size_t>(code) >= antiAliasingOptionCount) {
    return std::nullopt;
  }

  return allAntiAliasingOptions[static_cast<std::size_t>(code)];
}

inline constexpr std::optional<AntiAliasing> antiAliasingFromName(
    std::string_view name) {
  for (auto antiAliasing : allAntiAliasingOptions) {
    if (antiAliasingName(antiAliasing) == name) {
      return antiAliasing;
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
    case RenderScale::Eighth:
      divisor = 8;
      break;
    case RenderScale::Full:
      break;
  }

  return {
      (screenWidth + divisor - 1) / divisor,
      (screenHeight + divisor - 1) / divisor};
}

}  // namespace bw::app
