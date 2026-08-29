#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "InputOptions.h"
#include "VideoOptions.h"

class DLLState {
  int mNextStateFactory = 0;

public:
  // Applies the launcher's Input configuration. A sensitivity of zero would
  // freeze the player's view and a negative one would invert it by accident,
  // so both are rejected rather than silently applied.
  int setInputOptions(float mouseSensitivity, bw::app::InputOptions& inputOptions) const {
    if (!std::isfinite(mouseSensitivity) || mouseSensitivity <= 0.0f) {
      return 1;
    }

    inputOptions.mouseSensitivity = mouseSensitivity;

    return 0;
  }

  // Enum values cross the DLL boundary as integer codes. Apply only known
  // codes so a rejected update leaves the previously accepted options intact.
  int setVideoOptions(int renderScaleCode, int antiAliasingCode,
                      int ambientOcclusionCode, int renderTextureFilterCode,
                      int horizontalMaterialsCode,
                      int waterReflectionTechniqueCode,
                      int planarReflectionResolutionCode,
                      float playerTorchAttenuationRadius,
                      float playerTorchAttenuationFalloff,
                      int shadowsEnabledCode,
                      std::uint64_t shadowFaceResolution, float shadowRange,
                      float shadowNearPlane, float shadowConstantBias,
                      float shadowNormalBias, int shadowFilterCode,
                      float shadowFilterRadius, float shadowFadeStart,
                      bw::app::VideoOptions& videoOptions) const {
    auto renderScale = bw::app::renderScaleFromCode(renderScaleCode);
    auto antiAliasing = bw::app::antiAliasingFromCode(antiAliasingCode);
    auto ambientOcclusion =
        bw::app::ambientOcclusionFromCode(ambientOcclusionCode);
    auto renderTextureFilter =
        bw::app::renderTextureFilterFromCode(renderTextureFilterCode);
    auto horizontalMaterials =
        bw::app::horizontalMaterialsFromCode(horizontalMaterialsCode);
    auto waterReflectionTechnique =
        bw::app::waterReflectionTechniqueFromCode(
            waterReflectionTechniqueCode);
    auto planarReflectionResolution =
        bw::app::planarReflectionResolutionFromCode(
            planarReflectionResolutionCode);
    auto shadowFilter = bw::app::shadowFilterFromCode(shadowFilterCode);
    auto finite = [](float value) { return std::isfinite(value); };
    if (!renderScale || !antiAliasing || !ambientOcclusion ||
        !renderTextureFilter || !horizontalMaterials ||
        !waterReflectionTechnique || !planarReflectionResolution ||
        !shadowFilter ||
        !finite(playerTorchAttenuationRadius) ||
        !finite(playerTorchAttenuationFalloff) ||
        playerTorchAttenuationRadius <= 0.0f ||
        playerTorchAttenuationFalloff < 0.0f ||
        playerTorchAttenuationFalloff > playerTorchAttenuationRadius ||
        (shadowsEnabledCode != 0 && shadowsEnabledCode != 1) ||
        shadowFaceResolution == 0 ||
        shadowFaceResolution > std::numeric_limits<std::size_t>::max() ||
        !finite(shadowRange) || !finite(shadowNearPlane) ||
        !finite(shadowConstantBias) || !finite(shadowNormalBias) ||
        !finite(shadowFilterRadius) || !finite(shadowFadeStart) ||
        shadowNearPlane <= 0.0f || shadowRange <= shadowNearPlane ||
        shadowConstantBias < 0.0f || shadowNormalBias < 0.0f ||
        shadowFilterRadius < 0.0f || shadowFadeStart < 0.0f ||
        shadowFadeStart > 1.0f) {
      return 1;
    }

    // Construct the complete candidate first. A failure above therefore cannot
    // apply a valid prefix of a rejected boundary call.
    auto candidate = videoOptions;
    candidate.renderScale = *renderScale;
    candidate.antiAliasing = *antiAliasing;
    candidate.ambientOcclusion = *ambientOcclusion;
    candidate.renderTextureFilter = *renderTextureFilter;
    candidate.horizontalMaterials = *horizontalMaterials;
    candidate.waterReflections = {
        *waterReflectionTechnique, *planarReflectionResolution};
    candidate.playerTorch = {
        playerTorchAttenuationRadius, playerTorchAttenuationFalloff};
    candidate.shadows = {
        shadowsEnabledCode != 0,
        static_cast<std::size_t>(shadowFaceResolution),
        shadowRange,
        shadowNearPlane,
        shadowConstantBias,
        shadowNormalBias,
        *shadowFilter,
        shadowFilterRadius,
        shadowFadeStart};
    videoOptions = candidate;
    return 0;
  }

  // Convenience for callers that are testing only the named enum vocabulary.
  int setVideoOptions(int renderScaleCode, int antiAliasingCode,
                      int ambientOcclusionCode, int renderTextureFilterCode,
                      int horizontalMaterialsCode,
                      int waterReflectionTechniqueCode,
                      int planarReflectionResolutionCode,
                      bw::app::VideoOptions& videoOptions) const {
    auto const& shadows = videoOptions.shadows;
    return setVideoOptions(
        renderScaleCode, antiAliasingCode, ambientOcclusionCode,
        renderTextureFilterCode, horizontalMaterialsCode,
        waterReflectionTechniqueCode, planarReflectionResolutionCode,
        videoOptions.playerTorch.attenuationRadius,
        videoOptions.playerTorch.attenuationFalloff,
        shadows.enabled ? 1 : 0, shadows.faceResolution, shadows.range,
        shadows.nearPlane, shadows.constantBias, shadows.normalBias,
        bw::app::shadowFilterCode(shadows.filter), shadows.filterRadius,
        shadows.fadeStart, videoOptions);
  }

  int setVideoOptions(int renderScaleCode, int antiAliasingCode,
                      int ambientOcclusionCode, int renderTextureFilterCode,
                      int horizontalMaterialsCode,
                      bw::app::VideoOptions& videoOptions) const {
    return setVideoOptions(
        renderScaleCode, antiAliasingCode, ambientOcclusionCode,
        renderTextureFilterCode, horizontalMaterialsCode,
        bw::app::waterReflectionTechniqueCode(
            videoOptions.waterReflections.technique),
        bw::app::planarReflectionResolutionCode(
            videoOptions.waterReflections.planarResolution),
        videoOptions);
  }

  int setArgument(char const* arg, char const* value, bool& threadedLoading) const {
    if (std::strcmp(arg, "ThreadedLoading") != 0) {
      return 1;
    }

    std::string const argumentValue(value);
    if (argumentValue == "true") {
      threadedLoading = true;
    } else if (argumentValue == "false") {
      threadedLoading = false;
    } else {
      return 1;
    }

    return 0;
  }

  int getNextStateFactoryIndex() {
    return mNextStateFactory++;
  }

  void resetStateFactoryEnumeration() {
    mNextStateFactory = 0;
  }
};
