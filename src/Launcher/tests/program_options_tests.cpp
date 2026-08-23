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
                                         std::string const& renderScaleLine = "",
                                         std::string const& antiAliasingLine = "",
                                         std::string const& renderTextureFilterLine = "",
                                         std::string const& ambientOcclusionLine = "",
                                         std::string const& horizontalMaterialsLine = "") {
  auto path = std::filesystem::temp_directory_path() / "boolean-world-program-options-test.yaml";
  std::ofstream stream(path);
  stream << "Configuration:\n"
            "  Video:\n"
            "    Width: 1024\n"
            "    Height: 768\n"
            "    Fullscreen: false\n"
            "    VSync: true\n"
         << renderScaleLine
         << antiAliasingLine
         << renderTextureFilterLine
         << ambientOcclusionLine
         << horizontalMaterialsLine
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

ProgramOptions parseWithAntiAliasing(std::string const& antiAliasingLine) {
  auto path = writeConfiguration("", "", "", antiAliasingLine);
  try {
    auto options = parseProgramOptions(path.string());
    std::filesystem::remove(path);
    return options;
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
}

ProgramOptions parseWithRenderTextureFilter(std::string const& filterLine) {
  auto path = writeConfiguration("", "", "", "", filterLine);
  try {
    auto options = parseProgramOptions(path.string());
    std::filesystem::remove(path);
    return options;
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
}

ProgramOptions parseWithAmbientOcclusion(
    std::string const& ambientOcclusionLine) {
  auto path = writeConfiguration(
      "", "", "", "", "", ambientOcclusionLine);
  try {
    auto options = parseProgramOptions(path.string());
    std::filesystem::remove(path);
    return options;
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
}

ProgramOptions parseWithHorizontalMaterials(std::string const& line) {
  auto path = writeConfiguration("", "", "", "", "", "", line);
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

void requireAntiAliasingRejected(
    std::string const& antiAliasingLine,
    std::string const& description) {
  try {
    (void)parseWithAntiAliasing(antiAliasingLine);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("AA") != std::string::npos,
            "The anti-aliasing error did not name the field.");
    return;
  }
  throw std::runtime_error("Video configuration accepted " + description + ".");
}

void requireRenderTextureFilterRejected(
    std::string const& filterLine,
    std::string const& description) {
  try {
    (void)parseWithRenderTextureFilter(filterLine);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("RenderTextureFilter") != std::string::npos,
            "The render-texture filter error did not name the field.");
    return;
  }
  throw std::runtime_error("Video configuration accepted " + description + ".");
}

void requireHorizontalMaterialsRejected(
    std::string const& line, std::string const& description) {
  try {
    (void)parseWithHorizontalMaterials(line);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("HorizontalMaterials") !=
                std::string::npos,
            "The horizontal-material error did not name the field.");
    return;
  }
  throw std::runtime_error(
      "Video configuration accepted " + description + ".");
}

void requireAmbientOcclusionRejected(
    std::string const& ambientOcclusionLine,
    std::string const& description) {
  try {
    (void)parseWithAmbientOcclusion(ambientOcclusionLine);
  } catch (std::exception const& error) {
    require(std::string(error.what()).find("AmbientOcclusion") != std::string::npos,
            "The ambient-occlusion error did not name the field.");
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
    require(parseWithRenderScale("    RenderScale: EiGhTh\n").video.renderScale == bw::app::RenderScale::Eighth,
            "The configured eighth render scale did not parse case-insensitively.");
    requireRenderScaleRejected("    RenderScale: sixteenth\n", "an unknown render scale");
    requireRenderScaleRejected("    RenderScale: 2\n", "a numeric render scale");
    requireRenderScaleRejected("    RenderScale:\n", "an empty render scale");

    require(parseWithAntiAliasing("").video.antiAliasing == bw::app::AntiAliasing::Off,
            "A configuration without AA did not default to off.");
    require(parseWithAntiAliasing("    AA: MSAA-2X\n").video.antiAliasing == bw::app::AntiAliasing::Msaa2x,
            "The configured 2x MSAA did not parse case-insensitively.");
    require(parseWithAntiAliasing("    AA: msaa-4x\n").video.antiAliasing == bw::app::AntiAliasing::Msaa4x,
            "The configured 4x MSAA did not parse.");
    require(parseWithAntiAliasing("    AA: msaa-8x\n").video.antiAliasing == bw::app::AntiAliasing::Msaa8x,
            "The configured 8x MSAA did not parse.");
    require(parseWithAntiAliasing("    AA: FXAA\n").video.antiAliasing == bw::app::AntiAliasing::Fxaa,
            "The configured FXAA did not parse case-insensitively.");
    requireAntiAliasingRejected("    AA: msaa-16x\n", "an unsupported MSAA sample count");
    requireAntiAliasingRejected("    AA: 4\n", "a numeric AA setting");
    requireAntiAliasingRejected("    AA:\n", "an empty AA setting");

    require(parseWithAmbientOcclusion("").video.ambientOcclusion ==
                bw::app::AmbientOcclusion::GtaoDepth,
            "A configuration without AmbientOcclusion did not default to depth-normal GTAO.");
    require(parseWithAmbientOcclusion("    AmbientOcclusion: SsAo\n")
                    .video.ambientOcclusion == bw::app::AmbientOcclusion::Ssao,
            "The configured SSAO method did not parse case-insensitively.");
    require(parseWithAmbientOcclusion("    AmbientOcclusion: GTAO-DEPTH\n")
                    .video.ambientOcclusion ==
                bw::app::AmbientOcclusion::GtaoDepth,
            "The configured depth-normal GTAO method did not parse case-insensitively.");
    require(parseWithAmbientOcclusion("    AmbientOcclusion: gtao-normals\n")
                    .video.ambientOcclusion ==
                bw::app::AmbientOcclusion::GtaoNormals,
            "The configured MRT-normal GTAO method did not parse.");
    require(parseWithAmbientOcclusion("    AmbientOcclusion: none\n")
                    .video.ambientOcclusion == bw::app::AmbientOcclusion::None,
            "The configured disabled ambient occlusion did not parse.");
    requireAmbientOcclusionRejected(
        "    AmbientOcclusion: gtao\n", "the retired GTAO spelling");
    requireAmbientOcclusionRejected(
        "    AmbientOcclusion: hbao\n", "an unknown ambient-occlusion method");
    requireAmbientOcclusionRejected(
        "    AmbientOcclusion:\n", "an empty ambient-occlusion method");

    require(parseWithRenderTextureFilter("").video.renderTextureFilter ==
                bw::app::RenderTextureFilter::Linear,
            "A configuration without RenderTextureFilter did not default to linear.");
    require(parseWithRenderTextureFilter("    RenderTextureFilter: NeArEsT\n").video.renderTextureFilter ==
                bw::app::RenderTextureFilter::Nearest,
            "The configured nearest filter did not parse case-insensitively.");
    requireRenderTextureFilterRejected(
        "    RenderTextureFilter: bilinear\n", "an unknown render-texture filter");
    requireRenderTextureFilterRejected(
        "    RenderTextureFilter:\n", "an empty render-texture filter");

    require(parseWithHorizontalMaterials("").video.horizontalMaterials ==
                bw::app::HorizontalMaterials::TwoDimensional,
            "A configuration without HorizontalMaterials did not default to 2d.");
    require(parseWithHorizontalMaterials("    HorizontalMaterials: 3D\n")
                    .video.horizontalMaterials ==
                bw::app::HorizontalMaterials::ThreeDimensional,
            "The configured 3d horizontal materials did not parse case-insensitively.");
    requireHorizontalMaterialsRejected(
        "    HorizontalMaterials: planar\n", "an unknown horizontal-material mode");
    requireHorizontalMaterialsRejected(
        "    HorizontalMaterials:\n", "an empty horizontal-material mode");

    std::cout << "Program-options schema validation passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
