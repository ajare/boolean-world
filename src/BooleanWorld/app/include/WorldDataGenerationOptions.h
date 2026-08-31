#pragma once

#include <optional>
#include <string_view>

namespace bw::app {

enum class WorldDataGenerationMode {
  Asynchronous,
  Synchronous
};

constexpr int worldDataGenerationModeCode(WorldDataGenerationMode mode) {
  return static_cast<int>(mode);
}

constexpr std::optional<WorldDataGenerationMode>
worldDataGenerationModeFromCode(int code) {
  switch (code) {
    case worldDataGenerationModeCode(WorldDataGenerationMode::Asynchronous):
      return WorldDataGenerationMode::Asynchronous;
    case worldDataGenerationModeCode(WorldDataGenerationMode::Synchronous):
      return WorldDataGenerationMode::Synchronous;
    default:
      return std::nullopt;
  }
}

constexpr std::string_view worldDataGenerationModeName(
    WorldDataGenerationMode mode) {
  switch (mode) {
    case WorldDataGenerationMode::Asynchronous:
      return "Asynchronous";
    case WorldDataGenerationMode::Synchronous:
      return "Synchronous";
  }
  return "";
}

constexpr std::optional<WorldDataGenerationMode>
worldDataGenerationModeFromName(std::string_view name) {
  if (name == "asynchronous") return WorldDataGenerationMode::Asynchronous;
  if (name == "synchronous") return WorldDataGenerationMode::Synchronous;
  return std::nullopt;
}

// Application-run policy for generating WorldData. The launcher transfers
// these values through a dedicated scalar DLL export rather than string args.
struct WorldDataGenerationOptions {
  WorldDataGenerationMode mode{WorldDataGenerationMode::Asynchronous};
  float startInterval{5.0f};
  bool alwaysUpdateVertices{false};
  bool allowCommitIfVisible{false};

  bool operator==(WorldDataGenerationOptions const&) const = default;
};

}  // namespace bw::app
