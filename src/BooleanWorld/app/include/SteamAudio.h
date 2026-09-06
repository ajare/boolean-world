#pragma once

#include <memory>

#include <mpp/Camera.h>

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

// Owns the process-wide Steam Audio objects required by the FMOD plugin. The
// plugin itself remains owned by FMOD's core system.
class SteamAudio {
public:
  explicit SteamAudio(wp::application::AudioSystem& audioSystem);
  ~SteamAudio();

  SteamAudio(SteamAudio const&) = delete;
  SteamAudio& operator=(SteamAudio const&) = delete;

  void setListener(mpp::Camera const& camera);

  // Runs the cheap, latency-sensitive direct/occlusion pass on the caller's
  // game thread. If the reflection worker is changing scenes, this tick is
  // skipped rather than hitching the game thread behind that handoff.
  void runDirectSimulation();

  // Builds only when sourceWorld differs from the last published snapshot,
  // then atomically publishes the committed scene and its source as one unit.
  void updateWorldSnapshot(
      core::ArrangementWorldDataPtr sourceWorld,
      AcousticPresetResolver const& resolver);

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
