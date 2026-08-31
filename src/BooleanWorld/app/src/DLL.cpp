#include <cstdint>

#include <willpower/common/Logger.h>

#include <willpower/application/StateFactory.h>

#include <applib/ModelInstance.h>
#include <applib/StateLoad.h>
#include <applib/StateUnload.h>
#include <applib/StateMapLoad.h>
#include <applib/StateMapUnload.h>
#include <applib/StateMapTransition.h>
#include <applib/MapDefaultDefinitionFactory.h>
#include <applib/MapTiledDefinitionFactory.h>
#include <applib/ProtoEntityDefaultDefinitionFactory.h>
#include <applib/ImageSetTiledDefinitionFactory.h>

#include "DLLState.h"
#include "InputOptions.h"
#include "VideoOptions.h"
#include "WorldDataGenerationOptions.h"
#include "MapBooleanWorldDefinitionFactory.h"
#include "ProtoEntityDefinitionFactory.h"

// Model
#include "BooleanWorldModel.h"
#include "EntityHandlerBooleanWorld.h"

// States
#include "StateMapLoadBooleanWorld.h"
#include "StateMapUnloadBooleanWorld.h"
#include "StateMapTransitionBooleanWorld.h"
#include "StateControllerBooleanWorld.h"
#include "StatePlayBooleanWorld.h"

// Resources
#include "Map.h"
#include "EmbossingCatalog.h"
#include "EmbossingCatalogResourceDefinitionFactory.h"
#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"
#include "ProtoEntity.h"

using namespace std;

// Model
static applib::Model* model = nullptr;

// State factories
static DLLState dllState;
static StateControllerBooleanWorldFactory* stateControllerFactory = nullptr;
static applib::StateLoadFactory* stateLoadFactory = nullptr;
static applib::StateUnloadFactory* stateUnloadFactory = nullptr;
static applib::StateMapLoadFactory* stateMapLoadFactory = nullptr;
static applib::StateMapUnloadFactory* stateMapUnloadFactory = nullptr;
static applib::StateMapTransitionFactory* stateMapTransitionFactory = nullptr;
static StatePlayBooleanWorldFactory* statePlayBooleanWorldFactory = nullptr;

// Arguments
static bool gThreadedLoading = true;

// Input configuration, supplied by the launcher before dllOnEntry so the
// entity handler can be built with it.
static bw::app::InputOptions gInputOptions;

// Video configuration is validated before entry and seeds the model, whose
// active scale then survives every map-owned renderer.
static bw::app::VideoOptions gVideoOptions;

// World data Generation configuration seeds the application-run model before
// any map can perform its mandatory bootstrap Generation.
static bw::app::WorldDataGenerationOptions gWorldDataGenerationOptions;

extern "C" {
__declspec(dllexport) char const* dllGetName() {
  return "BooleanWorld";
}

__declspec(dllexport) int dllSetArgument(char const* arg, char const* value) {
  return dllState.setArgument(arg, value, gThreadedLoading);
}

__declspec(dllexport) int dllSetInputOptions(float mouseSensitivity) {
  return dllState.setInputOptions(mouseSensitivity, gInputOptions);
}

__declspec(dllexport) int dllSetWorldDataGenerationOptions(
    int modeCode, float startInterval, int alwaysUpdateVerticesCode,
    int allowCommitIfVisibleCode) {
  return dllState.setWorldDataGenerationOptions(
      modeCode, startInterval, alwaysUpdateVerticesCode,
      allowCommitIfVisibleCode, gWorldDataGenerationOptions);
}

__declspec(dllexport) int dllSetVideoOptions(
    int renderScaleCode,
    int antiAliasingCode,
    int ambientOcclusionCode,
    int renderTextureFilterCode,
    int horizontalMaterialsCode,
    int waterReflectionTechniqueCode,
    int planarReflectionResolutionCode,
    float playerTorchAttenuationRadius,
    float playerTorchAttenuationFalloff,
    int shadowsEnabledCode,
    std::uint64_t shadowFaceResolution,
    float shadowRange,
    float shadowNearPlane,
    float shadowConstantBias,
    float shadowNormalBias,
    int shadowFilterCode,
    float shadowFilterRadius,
    float shadowFadeStart) {
  return dllState.setVideoOptions(
      renderScaleCode, antiAliasingCode, ambientOcclusionCode,
      renderTextureFilterCode, horizontalMaterialsCode,
      waterReflectionTechniqueCode, planarReflectionResolutionCode,
      playerTorchAttenuationRadius, playerTorchAttenuationFalloff,
      shadowsEnabledCode, shadowFaceResolution, shadowRange, shadowNearPlane,
      shadowConstantBias, shadowNormalBias, shadowFilterCode,
      shadowFilterRadius, shadowFadeStart,
      gVideoOptions);
}

__declspec(dllexport) wp::application::StateFactory* dllGetNextStateFactory() {
  wp::application::StateFactory* stateFactory;
  switch (dllState.getNextStateFactoryIndex()) {
    case 0:
      stateFactory = stateControllerFactory;
      break;
    case 1:
      stateFactory = stateLoadFactory;
      break;
    case 2:
      stateFactory = stateUnloadFactory;
      break;
    case 3:
      stateFactory = stateMapLoadFactory;
      break;
    case 4:
      stateFactory = stateMapUnloadFactory;
      break;
    case 5:
      stateFactory = stateMapTransitionFactory;
      break;
    case 6:
      stateFactory = statePlayBooleanWorldFactory;
      break;
    default:
      stateFactory = nullptr;
      break;
  }

  return stateFactory;
}

__declspec(dllexport) void dllOnEntry(wp::Logger* logger, wp::application::resourcesystem::ResourceManager* resourceMgr) {
  dllState.resetStateFactoryEnumeration();

  auto entityHandlerFactory = [](shared_ptr<applib::AnimationDatabase> animDatabase) {
    return new EntityHandlerBooleanWorld(animDatabase, gInputOptions);
  };

  model = new BooleanWorldModel(
      entityHandlerFactory, resourceMgr, gVideoOptions,
      gWorldDataGenerationOptions);
  applib::ModelInstance::set(model);

  // Create state factories
  stateControllerFactory = new StateControllerBooleanWorldFactory(logger);
  stateLoadFactory = new applib::StateLoadFactory(logger, resourceMgr, gThreadedLoading);
  stateUnloadFactory = new applib::StateUnloadFactory(logger, resourceMgr, gThreadedLoading);
  stateMapLoadFactory = new StateMapLoadBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
  stateMapUnloadFactory = new StateMapUnloadBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
  stateMapTransitionFactory = new StateMapTransitionBooleanWorldFactory(logger, resourceMgr, gThreadedLoading);
  statePlayBooleanWorldFactory = new StatePlayBooleanWorldFactory(logger);

  // Add resource factories
  resourceMgr->addResourceFactory(new MapResourceFactory(logger));
  resourceMgr->addResourceFactory(new ProtoEntityResourceFactory(model->entityHandler, model->animationDatabase));
  resourceMgr->addResourceFactory(new ProcMaterialResourceFactory());
  resourceMgr->addResourceFactory(new EmbossingCatalogResourceFactory());

  // Add resource definition factories
  resourceMgr->addResourceDefinitionFactory(new MapBooleanWorldDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new applib::MapTiledDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new ProtoEntityDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new applib::ImageSetTiledDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new ProcMaterialResourceDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(
      new EmbossingCatalogResourceDefinitionFactory());
}

__declspec(dllexport) void dllOnExit() {
  // Destroy state factories
  delete stateControllerFactory;
  stateControllerFactory = nullptr;

  delete stateLoadFactory;
  stateLoadFactory = nullptr;

  delete stateUnloadFactory;
  stateUnloadFactory = nullptr;

  delete stateMapLoadFactory;
  stateMapLoadFactory = nullptr;

  delete stateMapUnloadFactory;
  stateMapUnloadFactory = nullptr;

  delete stateMapTransitionFactory;
  stateMapTransitionFactory = nullptr;

  delete statePlayBooleanWorldFactory;
  statePlayBooleanWorldFactory = nullptr;

  // Model
  delete model;
  model = nullptr;
}
}