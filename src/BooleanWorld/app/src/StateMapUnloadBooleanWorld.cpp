#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>

#include "StateMapUnloadBooleanWorld.h"
#include "WorldRenderer.h"

using namespace std;
using namespace wp;

StateMapUnloadBooleanWorld::StateMapUnloadBooleanWorld(bool useThreading)
    : applib::StateMapUnload(useThreading) {
}

vector<applib::ThreadableLoadState::ThreadableWorkFunction> StateMapUnloadBooleanWorld::getPreWork(applib::StateTransitionData* transitionData) {
  auto worldRenderer = static_cast<WorldRenderer*>(transitionData->userData);

  // The renderer goes away on the threadable pre-work path, so its targets are
  // held here until the main-thread post-work step can release them.
  mRetiredWorldTargets = worldRenderer
                             ? worldRenderer->detachRenderTargets()
                             : WorldRenderer::RenderTargets{};

  transitionData->userData = nullptr;
  mTransitionData.userData = nullptr;

  auto destroyWorldRendererFn = [this, worldRenderer](bool useThreading) {
    VAR_UNUSED(useThreading);

    addText("Destroying world renderer");

    delete worldRenderer;
  };

  return {destroyWorldRendererFn};
}

void StateMapUnloadBooleanWorld::unloadResources(application::resourcesystem::ResourceManager* resourceMgr, applib::MapTransitionData* transitionData) {
  applib::StateMapUnload::unloadResources(resourceMgr, transitionData);

  auto releaseWorldTargetFn = [this](bool useThreading) {
    VAR_UNUSED(useThreading);

    addText("Releasing world render targets");

    for (auto& target : mRetiredWorldTargets) {
      target.reset();
    }
  };

  processPostWork({releaseWorldTargetFn});
}