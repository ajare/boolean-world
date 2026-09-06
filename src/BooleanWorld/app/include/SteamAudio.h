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
