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

std::filesystem::path writeConfiguration(std::string const& extraGameField) {
  auto path = std::filesystem::temp_directory_path() / "boolean-world-program-options-test.yaml";
  std::ofstream stream(path);
  stream << "Configuration:\n"
            "  Video:\n"
            "    Width: 1024\n"
            "    Height: 768\n"
            "    Fullscreen: false\n"
            "    VSync: true\n"
            "  Audio:\n"
            "    Enabled: false\n"
            "    Channels: 32\n"
            "    Sync: false\n"
            "  Game:\n"
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
}  // namespace

int main() {
  try {
    auto validPath = writeConfiguration("");
    auto options = parseProgramOptions(validPath.string());
    std::filesystem::remove(validPath);
    require(options.dll == "BooleanWorld.dll", "Valid configuration did not parse.");

    requireRejected("GameResource");
    requireRejected("MisspelledOption");

    std::cout << "Program-options schema validation passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
