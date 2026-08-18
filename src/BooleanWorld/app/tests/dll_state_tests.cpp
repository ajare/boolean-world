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

  require(state.setVideoOptions(bw::app::renderScaleCode(bw::app::RenderScale::Quarter), options) == 0,
          "Valid render scale was rejected.");
  require(options.renderScale == bw::app::RenderScale::Quarter,
          "Valid render scale was not applied.");

  require(state.setVideoOptions(-1, options) != 0,
          "Negative render scale code was accepted.");
  require(state.setVideoOptions(static_cast<int>(bw::app::renderScaleCount), options) != 0,
          "Out-of-range render scale code was accepted.");
  require(options.renderScale == bw::app::RenderScale::Quarter,
          "Rejected render scale changed configuration.");
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

  require(!bw::app::renderScaleFromName("eighth"),
          "Unknown render scale name was accepted.");
  auto exact = bw::app::renderTargetSize(1024, 768, bw::app::RenderScale::Half);
  require(exact.width == 512 && exact.height == 384,
          "Evenly divisible target dimensions were incorrect.");
  auto rounded = bw::app::renderTargetSize(1025, 769, bw::app::RenderScale::Quarter);
  require(rounded.width == 257 && rounded.height == 193,
          "Sub-full target dimensions did not round up.");
  auto tiny = bw::app::renderTargetSize(1, 1, bw::app::RenderScale::Quarter);
  require(tiny.width == 1 && tiny.height == 1,
          "A small screen produced a zero-sized render target.");
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
