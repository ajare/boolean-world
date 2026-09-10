#include "Screenshot.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <GL/glew.h>

#include <mpp/RenderSystem.h>

#include <utils/Image.h>

namespace bw::app {
namespace {

bool screenshotRequested = false;
std::optional<std::string> screenshotResult;

}  // namespace

void requestScreenshot() {
  screenshotRequested = true;
}

void captureScreenshotIfRequested(mpp::RenderSystem* renderSystem) {
  if (!std::exchange(screenshotRequested, false)) {
    return;
  }

  namespace fs = std::filesystem;
  using namespace std::chrono;

  auto const width = renderSystem->getWindowWidth();
  auto const height = renderSystem->getWindowHeight();
  if (width == 0 || height == 0) {
    screenshotResult =
        "Could not save screenshot: the window has no drawable area.";
    return;
  }

  try {
    renderSystem->flushVertexBuffers();

    std::vector<std::uint8_t> pixels(width * height * 3);
    GLint previousPackAlignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
        GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    auto const rowSize = width * 3;
    for (std::size_t y = 0; y < height / 2; ++y) {
      auto top = pixels.begin() + y * rowSize;
      auto bottom = pixels.begin() + (height - y - 1) * rowSize;
      std::swap_ranges(top, top + rowSize, bottom);
    }

    auto const now = system_clock::now();
    auto const time = system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    auto const millisecondsPart =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream filename;
    filename << "BooleanWorld_"
             << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S-")
             << std::setfill('0') << std::setw(3) << millisecondsPart
             << ".png";

    auto const shotsDirectory = fs::current_path() / "shots";
    fs::create_directories(shotsDirectory);
    auto const filepath = shotsDirectory / filename.str();

    utils::Image image;
    image.loadFromData(width, height, 24, pixels.data());
    image.saveToFile(filepath.string());
    screenshotResult = "Saved screenshot to " + filepath.string();
  } catch (std::exception const& exception) {
    screenshotResult =
        "Could not save screenshot: " + std::string(exception.what());
  }
}

std::optional<std::string> takeScreenshotResult() {
  return std::exchange(screenshotResult, std::nullopt);
}

}  // namespace bw::app
