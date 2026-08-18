#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ProgramOptions.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

std::filesystem::path writeConfiguration(std::string const& extraGameField,
                                         std::string const& inputSection = "",
                                         std::string const& renderScaleLine = "") {
  auto path = std::filesystem::temp_directory_path() / "boolean-world-program-options-test.yaml";
  std::ofstream stream(path);
  stream << "Configuration:\n"
            "  Video:\n"
            "    Width: 1024\n"
            "    Height: 768\n"
            "    Fullscreen: false\n"
            "    VSync: true\n"
         << renderScaleLine
         << "  Audio:\n"
            "    Enabled: false\n"
            "    Channels: 32\n"
            "    Sync: false\n"
         << inputSection
         << "  Game:\n"
            "    DLL:\n"
            "      path: BooleanWorld.dll\n"
            "    ResourceLocations:\n"
            "      ResourceLocation:\n"
            "        type: Directory\n"
            "        path: resources\n"
            "        definition: Resources.yaml\n";
  if (!extraGameField.empty()) {
    stream << "    " << extraGameField << ": retired\n";
  }
  return path;
}

void requireRejected(std::string const& field) {
  auto path = writeConfiguration(field);
  try {
    (void)parseProgramOptions(path.string());
  } catch (std::exception const& error) {
    require(std::string(error.what()).find(field) != std::string::npos,
            "The unknown-field error did not identify '" + field + "'.");
    std::filesystem::remove(path);
    return;
  }
  std::filesystem::remove(path);
  throw std::runtime_error("Game configuration accepted unsupported field '" + field + "'.");
}

ProgramOptions parseWithInput(std::string const& inputSection) {
  auto path = writeConfiguration("", inputSection);
  try {
    auto options = parseProgramOptions(path.string());
    std::filesystem::remove(path);
    return options;
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
}

ProgramOptions parseWithRenderScale(std::string const& renderScaleLine) {
  auto path = writeConfiguration("", "", renderScaleLine);
  try {
    auto options = parseProgramOptions(path.string());
    std::filesystem::remove(path);
    return options;
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
}

void requireInputRejected(std::string const& inputSection, std::string const& description) {
  try {
    (void)parseWithInput(inputSection);
  } catch (std::exception const&) {
    return;
  }
  throw std::runtime_error("Input configuration accepted " + description + ".");
}

void requireRenderScaleRejected(std::string const& renderScaleLine, std::string const& description) {
  try {
    (void)parseWithRenderScale(renderScaleLine);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("RenderScale") != std::string::npos,
            "The render-scale error did not name the field.");
    return;
  }
  throw std::runtime_error("Video configuration accepted " + description + ".");
}
}  // namespace

int main() {
  try {
    auto validPath = writeConfiguration("");
    auto options = parseProgramOptions(validPath.string());
    std::filesystem::remove(validPath);
    require(options.dll == "BooleanWorld.dll", "Valid configuration did not parse.");

    requireRejected("GameResource");
    requireRejected("MisspelledOption");

    require(parseWithInput("").input.mouseSensitivity == 0.3f,
            "A configuration without an Input section did not default the mouse sensitivity.");
    require(parseWithInput("  Input:\n").input.mouseSensitivity == 0.3f,
            "An empty Input section did not default the mouse sensitivity.");
    require(parseWithInput("  Input:\n    MouseSensitivity: 2.5\n").input.mouseSensitivity == 2.5f,
            "The configured mouse sensitivity did not parse.");

    requireInputRejected("  Input:\n    MouseSensitivity: 0\n", "a zero mouse sensitivity");
    requireInputRejected("  Input:\n    MouseSensitivity: -1.5\n", "a negative mouse sensitivity");
    requireInputRejected("  Input:\n    MouseSensitivity: fast\n", "an unparseable mouse sensitivity");
    requireInputRejected("  Input:\n    MouseSensitivty: 2.0\n", "a misspelled input field");

    require(parseWithRenderScale("").video.renderScale == bw::app::RenderScale::Full,
            "A configuration without RenderScale did not default to full.");
    require(parseWithRenderScale("    RenderScale: HaLf\n").video.renderScale == bw::app::RenderScale::Half,
            "The configured half render scale did not parse case-insensitively.");
    require(parseWithRenderScale("    RenderScale: QUARTER\n").video.renderScale == bw::app::RenderScale::Quarter,
            "The configured quarter render scale did not parse case-insensitively.");
    requireRenderScaleRejected("    RenderScale: eighth\n", "an unknown render scale");
    requireRenderScaleRejected("    RenderScale: 2\n", "a numeric render scale");
    requireRenderScaleRejected("    RenderScale:\n", "an empty render scale");

    std::cout << "Program-options schema validation passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
