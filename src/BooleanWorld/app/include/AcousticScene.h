#pragma once

#include <memory>

#include <core/ArrangementWorldData.h>

#include "AcousticSceneMesh.h"

namespace bw::app {

// An immutable, committed Steam Audio scene and the exact World snapshot from
// which it was exported. Keeping the source reference here prevents a native
// scene from outliving or being mistaken for another generation's geometry.
class AcousticScene {
public:
  ~AcousticScene();

  AcousticScene(AcousticScene const&) = delete;
  AcousticScene& operator=(AcousticScene const&) = delete;

  [[nodiscard]] core::ArrangementWorldDataPtr const& getSourceWorld() const;
  [[nodiscard]] AcousticSceneMesh const& getExportedMesh() const;

private:
  friend class SteamAudio;
  struct Implementation;

  AcousticScene(
      core::ArrangementWorldDataPtr sourceWorld,
      AcousticSceneMesh mesh,
      std::unique_ptr<Implementation> implementation);

  core::ArrangementWorldDataPtr mSourceWorld;
  AcousticSceneMesh mMesh;
  std::unique_ptr<Implementation> mImplementation;
};

using AcousticScenePtr = std::shared_ptr<AcousticScene const>;

}  // namespace bw::app
