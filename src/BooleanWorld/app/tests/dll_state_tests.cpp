#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "DLLState.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void rejectsUnknownArguments() {
  DLLState state;
  bool threadedLoading = true;

  require(state.setArgument("ThreadedLoading", "false", threadedLoading) == 0,
          "Known DLL argument was rejected.");
  require(!threadedLoading, "Known DLL argument was not applied.");

  require(state.setArgument("ThreadedLoadng", "true", threadedLoading) != 0,
          "Unknown DLL argument was accepted.");
  require(!threadedLoading, "Unknown DLL argument changed configuration.");
}

void rejectsUnusableMouseSensitivities() {
  DLLState state;
  bw::app::InputOptions options;

  require(state.setInputOptions(2.5f, options) == 0, "Valid mouse sensitivity was rejected.");
  require(options.mouseSensitivity == 2.5f, "Valid mouse sensitivity was not applied.");

  require(state.setInputOptions(0.0f, options) != 0, "Zero mouse sensitivity was accepted.");
  require(state.setInputOptions(-1.0f, options) != 0, "Negative mouse sensitivity was accepted.");
  require(state.setInputOptions(std::numeric_limits<float>::quiet_NaN(), options) != 0,
          "Non-finite mouse sensitivity was accepted.");
  require(options.mouseSensitivity == 2.5f, "Rejected mouse sensitivity changed configuration.");
}

void acceptsAndValidatesRenderScaleCodes() {
  DLLState state;
  bw::app::VideoOptions options;
  auto full = bw::app::renderScaleCode(bw::app::RenderScale::Full);
  auto msaa2x = bw::app::antiAliasingCode(bw::app::AntiAliasing::Msaa2x);
  auto nearest = bw::app::renderTextureFilterCode(
      bw::app::RenderTextureFilter::Nearest);

  require(state.setVideoOptions(
              bw::app::renderScaleCode(bw::app::RenderScale::Quarter),
              bw::app::antiAliasingCode(bw::app::AntiAliasing::Fxaa),
              nearest, options) == 0,
          "Valid video options were rejected.");
  require(options.renderScale == bw::app::RenderScale::Quarter,
          "Valid render scale was not applied.");
  require(options.antiAliasing == bw::app::AntiAliasing::Fxaa,
          "Valid anti-aliasing setting was not applied.");
  require(options.renderTextureFilter == bw::app::RenderTextureFilter::Nearest,
          "Valid render-texture filter was not applied.");

  require(state.setVideoOptions(-1, msaa2x, nearest, options) != 0,
          "Negative render scale code was accepted.");
  require(state.setVideoOptions(
              static_cast<int>(bw::app::renderScaleCount),
              msaa2x, nearest, options) != 0,
          "Out-of-range render scale code was accepted.");
  require(state.setVideoOptions(full, -1, nearest, options) != 0,
          "Negative anti-aliasing code was accepted.");
  require(state.setVideoOptions(
              full, static_cast<int>(bw::app::antiAliasingOptionCount),
              nearest, options) != 0,
          "Out-of-range anti-aliasing code was accepted.");
  require(state.setVideoOptions(full, msaa2x, -1, options) != 0,
          "Negative render-texture filter code was accepted.");
  require(state.setVideoOptions(
              full, msaa2x,
              static_cast<int>(bw::app::renderTextureFilterCount),
              options) != 0,
          "Out-of-range render-texture filter code was accepted.");
  require(options.renderScale == bw::app::RenderScale::Quarter &&
              options.antiAliasing == bw::app::AntiAliasing::Fxaa &&
              options.renderTextureFilter == bw::app::RenderTextureFilter::Nearest,
          "Rejected video options changed configuration.");
}

void renderScaleVocabularyIsClosedAndSizesTargets() {
  for (auto scale : bw::app::allRenderScales) {
    auto fromName = bw::app::renderScaleFromName(bw::app::renderScaleName(scale));
    require(fromName && *fromName == scale,
            "Render scale did not round-trip through its name.");
    auto fromCode = bw::app::renderScaleFromCode(bw::app::renderScaleCode(scale));
    require(fromCode && *fromCode == scale,
            "Render scale did not round-trip through its boundary code.");
  }

  require(!bw::app::renderScaleFromName("sixteenth"),
          "Unknown render scale name was accepted.");
  for (auto antiAliasing : bw::app::allAntiAliasingOptions) {
    auto fromName = bw::app::antiAliasingFromName(
        bw::app::antiAliasingName(antiAliasing));
    require(fromName && *fromName == antiAliasing,
            "Anti-aliasing setting did not round-trip through its name.");
    auto fromCode = bw::app::antiAliasingFromCode(
        bw::app::antiAliasingCode(antiAliasing));
    require(fromCode && *fromCode == antiAliasing,
            "Anti-aliasing setting did not round-trip through its boundary code.");
  }
  require(!bw::app::antiAliasingFromName("msaa-16x"),
          "Unknown anti-aliasing setting was accepted.");
  for (auto filter : bw::app::allRenderTextureFilters) {
    auto fromName = bw::app::renderTextureFilterFromName(
        bw::app::renderTextureFilterName(filter));
    require(fromName && *fromName == filter,
            "Render-texture filter did not round-trip through its name.");
    auto fromCode = bw::app::renderTextureFilterFromCode(
        bw::app::renderTextureFilterCode(filter));
    require(fromCode && *fromCode == filter,
            "Render-texture filter did not round-trip through its boundary code.");
  }
  require(!bw::app::renderTextureFilterFromName("bilinear"),
          "Unknown render-texture filter was accepted.");
  auto exact = bw::app::renderTargetSize(1024, 768, bw::app::RenderScale::Half);
  require(exact.width == 512 && exact.height == 384,
          "Evenly divisible target dimensions were incorrect.");
  auto rounded = bw::app::renderTargetSize(1025, 769, bw::app::RenderScale::Quarter);
  require(rounded.width == 257 && rounded.height == 193,
          "Sub-full target dimensions did not round up.");
  auto eighth = bw::app::renderTargetSize(1025, 769, bw::app::RenderScale::Eighth);
  require(eighth.width == 129 && eighth.height == 97,
          "Eighth-scale target dimensions did not round up.");
  auto tiny = bw::app::renderTargetSize(1, 1, bw::app::RenderScale::Eighth);
  require(tiny.width == 1 && tiny.height == 1,
          "A small screen produced a zero-sized target.");
}

void resetsStateFactoryEnumerationOnEntry() {
  DLLState state;

  require(state.getNextStateFactoryIndex() == 0,
          "Initial state-factory enumeration did not start at the first factory.");
  require(state.getNextStateFactoryIndex() == 1,
          "State-factory enumeration did not advance.");

  state.resetStateFactoryEnumeration();

  require(state.getNextStateFactoryIndex() == 0,
          "State-factory enumeration did not reset for the next DLL entry.");
}

}  // namespace

int main() {
  try {
    rejectsUnknownArguments();
    rejectsUnusableMouseSensitivities();
    acceptsAndValidatesRenderScaleCodes();
    renderScaleVocabularyIsClosedAndSizesTargets();
    resetsStateFactoryEnumerationOnEntry();
    std::cout << "DLL state regression tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
