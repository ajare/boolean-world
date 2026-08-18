#pragma once

#include <array>
#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <mpp/RenderTarget.h>

#include <applib/StateMapUnload.h>

#include "Platform.h"
#include "VideoOptions.h"

class APPLICATION_API StateMapUnloadBooleanWorld : public applib::StateMapUnload {
  // Taken off the world renderer before pre-work tears it down, and dropped in
  // post-work: releasing render targets is OpenGL work, and post-work is the
  // step that runs on the main thread.
  std::array<mpp::RenderTargetPtr, bw::app::renderScaleCount> mRetiredWorldTargets;

protected:
  std::vector<ThreadableWorkFunction> getPreWork(applib::StateTransitionData* transitionData) override;

  void unloadResources(wp::application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData) override;

public:
  explicit StateMapUnloadBooleanWorld(bool useThreading);
};

class StateMapUnloadBooleanWorldFactory : public applib::StateMapUnloadFactory {
public:
  explicit StateMapUnloadBooleanWorldFactory(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr, bool useThreading)
      : applib::StateMapUnloadFactory(logger, resourceMgr, useThreading) {
  }

  wp::application::State* createState() {
    auto state = new StateMapUnloadBooleanWorld(mUseThreading);
    state->setLogger(mwLogger);
    return state;
  }
};
