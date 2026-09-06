#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bw::app {

// One player-facing acoustic quality choice. All fields change together so a
// quality adjustment cannot accidentally trade one simulation dimension
// against another.
struct AudioQualityPreset {
  std::string name{"Medium"};
  std::uint32_t rayCount{4096};
  std::uint32_t bounceCount{4};
  float impulseResponseDuration{1.0f};
  std::uint32_t ambisonicOrder{1};
  std::uint32_t reflectionSourceCap{1};
  float simulationUpdateRate{10.0f};

  auto operator<=>(AudioQualityPreset const&) const = default;
};

struct AudioFeatureOptions {
  bool occlusion{true};
  bool transmission{true};
  bool reflections{true};
  bool airAbsorption{true};

  auto operator<=>(AudioFeatureOptions const&) const = default;
};

struct AudioSimulationOptions {
  std::vector<AudioQualityPreset> presets{{}};
  std::string qualityPreset{"Medium"};
  bool qualityMayBeModifiedLive{true};
  AudioFeatureOptions features{};

  [[nodiscard]] AudioQualityPreset const* findPreset(
      std::string_view name) const {
    auto found = std::ranges::find(presets, name, &AudioQualityPreset::name);
    return found == presets.end() ? nullptr : &*found;
  }
};

// Process configuration received from Launcher before dllOnEntry.
[[nodiscard]] AudioSimulationOptions const& configuredAudioSimulationOptions();

}  // namespace bw::app
