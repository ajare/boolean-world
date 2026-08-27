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

// Screen-space ambient-occlusion method used by the world render pipeline.
enum class AmbientOcclusion : int {
  None = 0,
  Ssao = 1,
  GtaoDepth = 2,
  GtaoNormals = 3,
};

inline constexpr std::array<AmbientOcclusion, 4> allAmbientOcclusionOptions{
    AmbientOcclusion::None,
    AmbientOcclusion::Ssao,
    AmbientOcclusion::GtaoDepth,
    AmbientOcclusion::GtaoNormals};

inline constexpr std::size_t ambientOcclusionOptionCount =
    allAmbientOcclusionOptions.size();

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

// Dimensionality used for procedural materials on horizontal world surfaces.
// Walls always use the three-dimensional material shader.
enum class HorizontalMaterials : int {
  TwoDimensional = 0,
  ThreeDimensional = 1,
};

inline constexpr std::array<HorizontalMaterials, 2> allHorizontalMaterials{
    HorizontalMaterials::TwoDimensional,
    HorizontalMaterials::ThreeDimensional};

inline constexpr std::size_t horizontalMaterialsCount =
    allHorizontalMaterials.size();

// Filtering for the Player Torch's point-shadow cubemap. These values have
// stable boundary codes; the launcher never passes an MPP enum through the
// application DLL ABI.
enum class ShadowFilter : int {
  Hard = 0,
  Pcf = 1,
};

inline constexpr std::array<ShadowFilter, 2> allShadowFilters{
    ShadowFilter::Hard, ShadowFilter::Pcf};
inline constexpr std::size_t shadowFilterCount = allShadowFilters.size();

inline constexpr std::string_view shadowFilterName(ShadowFilter filter) {
  return filter == ShadowFilter::Hard ? "hard" : "pcf";
}

inline constexpr int shadowFilterCode(ShadowFilter filter) {
  return static_cast<int>(filter);
}

inline constexpr std::optional<ShadowFilter> shadowFilterFromCode(int code) {
  if (code < 0 || static_cast<std::size_t>(code) >= shadowFilterCount) {
    return std::nullopt;
  }
  return allShadowFilters[static_cast<std::size_t>(code)];
}

inline constexpr std::optional<ShadowFilter> shadowFilterFromName(
    std::string_view name) {
  for (auto filter : allShadowFilters) {
    if (shadowFilterName(filter) == name) return filter;
  }
  return std::nullopt;
}

struct ShadowOptions {
  bool enabled{true};
  std::size_t faceResolution{1024};
  float range{192.0f};
  float nearPlane{0.25f};
  float constantBias{0.0008f};
  float normalBias{0.0025f};
  ShadowFilter filter{ShadowFilter::Pcf};
  float filterRadius{1.0f};
  float fadeStart{0.9f};
};

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
  AmbientOcclusion ambientOcclusion{AmbientOcclusion::GtaoDepth};
  RenderTextureFilter renderTextureFilter{RenderTextureFilter::Linear};
  HorizontalMaterials horizontalMaterials{HorizontalMaterials::TwoDimensional};
  ShadowOptions shadows;
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

inline constexpr std::string_view horizontalMaterialsName(
    HorizontalMaterials materials) {
  return materials == HorizontalMaterials::ThreeDimensional ? "3d" : "2d";
}

inline constexpr int horizontalMaterialsCode(HorizontalMaterials materials) {
  return static_cast<int>(materials);
}

inline constexpr std::optional<HorizontalMaterials> horizontalMaterialsFromCode(
    int code) {
  if (code < 0 || static_cast<std::size_t>(code) >= horizontalMaterialsCount) {
    return std::nullopt;
  }
  return allHorizontalMaterials[static_cast<std::size_t>(code)];
}

inline constexpr std::optional<HorizontalMaterials> horizontalMaterialsFromName(
    std::string_view name) {
  for (auto materials : allHorizontalMaterials) {
    if (horizontalMaterialsName(materials) == name) {
      return materials;
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

inline constexpr std::string_view ambientOcclusionName(
    AmbientOcclusion ambientOcclusion) {
  switch (ambientOcclusion) {
    case AmbientOcclusion::Ssao:
      return "ssao";
    case AmbientOcclusion::GtaoDepth:
      return "gtao-depth";
    case AmbientOcclusion::GtaoNormals:
      return "gtao-normals";
    case AmbientOcclusion::None:
      break;
  }

  return "none";
}

inline constexpr int ambientOcclusionCode(AmbientOcclusion ambientOcclusion) {
  return static_cast<int>(ambientOcclusion);
}

inline constexpr std::optional<AmbientOcclusion> ambientOcclusionFromCode(
    int code) {
  if (code < 0 ||
      static_cast<std::size_t>(code) >= ambientOcclusionOptionCount) {
    return std::nullopt;
  }

  return allAmbientOcclusionOptions[static_cast<std::size_t>(code)];
}

inline constexpr std::optional<AmbientOcclusion> ambientOcclusionFromName(
    std::string_view name) {
  for (auto ambientOcclusion : allAmbientOcclusionOptions) {
    if (ambientOcclusionName(ambientOcclusion) == name) {
      return ambientOcclusion;
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
