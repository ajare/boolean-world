#pragma once

#include <cmath>
#include <cstring>
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
                      int renderTextureFilterCode,
                      bw::app::VideoOptions& videoOptions) const {
    auto renderScale = bw::app::renderScaleFromCode(renderScaleCode);
    auto antiAliasing = bw::app::antiAliasingFromCode(antiAliasingCode);
    auto renderTextureFilter =
        bw::app::renderTextureFilterFromCode(renderTextureFilterCode);
    if (!renderScale || !antiAliasing || !renderTextureFilter) {
      return 1;
    }

    videoOptions.renderScale = *renderScale;
    videoOptions.antiAliasing = *antiAliasing;
    videoOptions.renderTextureFilter = *renderTextureFilter;
    return 0;
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
