#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <mpp/Camera.h>

#include "AudioSimulationOptions.h"
#include "EmitterSourcePolicy.h"

namespace wp::application {
class AudioSystem;
}

namespace bw::core {
class ArrangementWorldData;
using ArrangementWorldDataPtr = std::shared_ptr<ArrangementWorldData const>;
}  // namespace bw::core

class AcousticPresetResolver;

namespace bw::app {
class AcousticScene;
using AcousticScenePtr = std::shared_ptr<AcousticScene const>;

struct AudioEmitterSimulationDiagnostics {
  EmitterSourceIdentity identity;
  std::string soundId;
  float normalizedRankingScore{};
  bool selectedForReflections{};
};

struct AudioSimulationDiagnostics {
  std::size_t capturedEmitterCount{};
  std::size_t inCullRangeEmitterCount{};
  std::size_t reflectionEmitterCount{};
  std::vector<AudioEmitterSimulationDiagnostics> rankedEmitters;
  double reflectionThreadCostMilliseconds{};
  double reflectionUpdateRateHz{};
};

// Owns the process-wide Steam Audio objects required by the FMOD plugin. The
// plugin itself remains owned by FMOD's core system.
class SteamAudio {
public:
  // Registers the Steam Audio DSP with FMOD before any bank that references it
  // is loaded. Safe to call again when constructing the simulation objects.
  static void loadPlugin(wp::application::AudioSystem& audioSystem);

  SteamAudio(
      wp::application::AudioSystem& audioSystem,
      AudioSimulationOptions const& options);
  ~SteamAudio();

  SteamAudio(SteamAudio const&) = delete;
  SteamAudio& operator=(SteamAudio const&) = delete;

  // Changes all quality dimensions atomically for subsequent simulation
  // ticks. Returns false for an unknown preset or when live changes are
  // disabled by Game.yaml.
  [[nodiscard]] bool setQualityPreset(std::string_view name);
  [[nodiscard]] std::string_view getQualityPreset() const;
  [[nodiscard]] bool qualityMayBeModifiedLive() const;
  [[nodiscard]] std::vector<AudioQualityPreset> const& getQualityPresets() const;
  [[nodiscard]] AudioQualityPreset const& getQualitySettings() const;

  [[nodiscard]] AudioFeatureOptions getFeatures() const;
  // Changes the simulator inputs without rebuilding its DSP graph. The
  // transmission-without-occlusion combination is rejected.
  [[nodiscard]] bool setFeatures(AudioFeatureOptions features);

  [[nodiscard]] AudioSimulationDiagnostics getDiagnostics() const;

  void setListener(mpp::Camera const& camera);

  // Reconciles captured emitters by (GUID, placement key). A matching active
  // source and its FMOD EventInstance survive a generation commit unchanged;
  // vanished sources begin a short whole-event fade.
  void syncEmitters(core::ArrangementWorldDataPtr sourceWorld);

  // Applies distance culling, starts/stops events, updates source simulation
  // inputs, and cross-fades reflection-budget transitions.
  void updateEmitters(glm::vec3 const& listenerPosition, float frameTime);

  // Runs the cheap, latency-sensitive direct/occlusion pass on the caller's
  // game thread. If the reflection worker is changing scenes, this tick is
  // skipped rather than hitching the game thread behind that handoff.
  void runDirectSimulation();

  // Builds only when sourceWorld differs from the last published snapshot,
  // then atomically publishes the committed scene and its source as one unit.
  void updateWorldSnapshot(
      core::ArrangementWorldDataPtr sourceWorld,
      AcousticPresetResolver const& resolver);

  // Publishes a scene whose expensive mesh export and native scene commit were
  // completed by the world-generation worker.
  [[nodiscard]] AcousticScenePtr publishWorldSnapshot(
      AcousticScenePtr scene);

  // Exports and commits a new immutable default triangle-mesh scene. The
  // returned object retains the exact source snapshot; it is never updated in
  // place when a later generation commits.
  [[nodiscard]] AcousticScenePtr buildScene(
      core::ArrangementWorldDataPtr sourceWorld,
      AcousticPresetResolver const& resolver) const;

private:
  struct Implementation;
  Implementation* mImplementation;
};

}  // namespace bw::app
