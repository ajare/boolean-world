#pragma once

#include <mpp/Camera.h>

namespace wp::application {
class AudioSystem;
}

namespace bw::app {

// Owns the process-wide Steam Audio objects required by the FMOD plugin. The
// plugin itself remains owned by FMOD's core system.
class SteamAudio {
public:
  explicit SteamAudio(wp::application::AudioSystem& audioSystem);
  ~SteamAudio();

  SteamAudio(SteamAudio const&) = delete;
  SteamAudio& operator=(SteamAudio const&) = delete;

  void setListener(mpp::Camera const& camera);

private:
  struct Implementation;
  Implementation* mImplementation;
};

}  // namespace bw::app
