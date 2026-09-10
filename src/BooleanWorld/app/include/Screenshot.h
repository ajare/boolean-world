#pragma once

#include <optional>
#include <string>

namespace mpp {
class RenderSystem;
}

namespace bw::app {

// Requests a readback after the complete frame, including launcher-rendered
// overlays such as ImGui, has been drawn.
void requestScreenshot();

// Called by the launcher after it has rendered all frame overlays.
void captureScreenshotIfRequested(mpp::RenderSystem* renderSystem);

// Returns the user-facing result of the latest completed capture, if any.
std::optional<std::string> takeScreenshotResult();

}  // namespace bw::app
