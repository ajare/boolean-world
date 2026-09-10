#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <willpower/common/Logger.h>

#include <willpower/application/StateFactory.h>

#include <core/LayerBuildStep.h>
#include <core-lua/CoreLua.h>

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

#include "AudioSimulationOptions.h"
#include "CpuUpdateProfiler.h"
#include "DLLState.h"
#include "InputOptions.h"
#include "VideoOptions.h"
#include "WorldDataGenerationOptions.h"
#include "MapBooleanWorldDefinitionFactory.h"
#include "ProtoEntityDefinitionFactory.h"
#include "SteamAudio.h"

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
#include "AcousticCatalog.h"
#include "AcousticCatalogResourceDefinitionFactory.h"
#include "Map.h"
#include "EmbossingCatalog.h"
#include "EmbossingCatalogResourceDefinitionFactory.h"
#include "ProcMaterial.h"
#include "ProcMaterialResourceDefinitionFactory.h"
#include "TriplanarMaterial.h"
#include "TriplanarMaterialResourceDefinitionFactory.h"
#include "ProtoEntity.h"

using namespace std;

// Model and process-wide build-script runtime.
static applib::Model* model = nullptr;
static unique_ptr<bw::core::ScriptRuntime> scriptRuntime;

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

// Steam Audio configuration is transferred as scalars across the DLL boundary
// before entry, then remains available to each play-state instance.
static bw::app::AudioSimulationOptions gAudioSimulationOptions;

bw::app::AudioSimulationOptions const&
bw::app::configuredAudioSimulationOptions() {
  return gAudioSimulationOptions;
}

// Video configuration is validated before entry and seeds the model, whose
// active scale then survives every map-owned renderer.
static bw::app::VideoOptions gVideoOptions;

// World data Generation configuration seeds the application-run model before
// any map can perform its mandatory bootstrap Generation.
static bw::app::WorldDataGenerationOptions gWorldDataGenerationOptions;

extern "C" {
APPLICATION_API char const* dllGetName() {
  return "BooleanWorld";
}

APPLICATION_API int dllSetArgument(char const* arg, char const* value) {
  return dllState.setArgument(arg, value, gThreadedLoading);
}

APPLICATION_API int dllSetInputOptions(float mouseSensitivity) {
  return dllState.setInputOptions(mouseSensitivity, gInputOptions);
}

APPLICATION_API void dllResetAudioSimulationOptions() {
  gAudioSimulationOptions.presets.clear();
}

APPLICATION_API int dllAddAudioQualityPreset(
    char const* name, std::uint32_t rayCount, std::uint32_t bounceCount,
    float impulseResponseDuration, std::uint32_t ambisonicOrder,
    std::uint32_t reflectionSourceCap, float simulationUpdateRate) {
  if (!name || !*name || rayCount == 0 || bounceCount == 0 ||
      !std::isfinite(impulseResponseDuration) ||
      impulseResponseDuration <= 0.0f || ambisonicOrder == 0 ||
      reflectionSourceCap == 0 || !std::isfinite(simulationUpdateRate) ||
      simulationUpdateRate <= 0.0f ||
      std::ranges::any_of(gAudioSimulationOptions.presets,
                          [name](auto const& preset) {
                            return preset.name == name;
                          })) {
    return 1;
  }
  gAudioSimulationOptions.presets.push_back(
      {name, rayCount, bounceCount, impulseResponseDuration, ambisonicOrder,
       reflectionSourceCap, simulationUpdateRate});
  return 0;
}

APPLICATION_API int dllSetAudioSimulationOptions(
    char const* qualityPreset, int qualityMayBeModifiedLive,
    int occlusion, int transmission, int reflections, int airAbsorption) {
  auto booleanCode = [](int value) { return value == 0 || value == 1; };
  if (!qualityPreset || !booleanCode(qualityMayBeModifiedLive) ||
      !booleanCode(occlusion) || !booleanCode(transmission) ||
      !booleanCode(reflections) || !booleanCode(airAbsorption) ||
      (!occlusion && transmission)) {
    return 1;
  }
  gAudioSimulationOptions.qualityPreset = qualityPreset;
  if (!gAudioSimulationOptions.findPreset(qualityPreset)) return 1;
  gAudioSimulationOptions.qualityMayBeModifiedLive =
      qualityMayBeModifiedLive != 0;
  gAudioSimulationOptions.features = {
      occlusion != 0, transmission != 0, reflections != 0,
      airAbsorption != 0};
  return 0;
}

APPLICATION_API int dllCpuUpdateTimingCaptureEnabled() {
  return bw::app::cpuUpdateProfiler().captureEnabled() ? 1 : 0;
}

APPLICATION_API void dllRecordCpuUpdateTimings(
    std::uint64_t gameNs, std::uint64_t audioNs) {
  bw::app::cpuUpdateProfiler().recordFrame(gameNs, audioNs);
}

APPLICATION_API int dllSetWorldDataGenerationOptions(
    int modeCode, float startInterval, int alwaysUpdateVerticesCode,
    int allowCommitIfVisibleCode) {
  return dllState.setWorldDataGenerationOptions(
      modeCode, startInterval, alwaysUpdateVerticesCode,
      allowCommitIfVisibleCode, gWorldDataGenerationOptions);
}

APPLICATION_API int dllSetVideoOptions(
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

APPLICATION_API wp::application::StateFactory* dllGetNextStateFactory() {
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

APPLICATION_API void dllOnEntry(
    wp::Logger* logger,
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::application::AudioSystem* audioSystem) {
  dllState.resetStateFactoryEnumeration();

  // FMOD must know the Steam Audio DSP before map loading creates banks whose
  // events reference it. The full simulation remains owned by the Play state.
  if (audioSystem) {
    bw::app::SteamAudio::loadPlugin(*audioSystem);
  }

  // docs/adr/0038: LayerBuildStep types are no longer compiled into core's
  // own registry, so every host must register the ones it wants Worlds to
  // be able to deserialize.
  bw::core::LayerBuildStep::registerCoreTypes();
  scriptRuntime = make_unique<bw::core::ScriptRuntime>(
      [logger](bw::core::ScriptLogEvent const& event) {
        if (event.type == bw::core::ScriptLogEventType::Output) {
          logger->info("Lua [" + event.scriptName + "]: " + event.message);
        } else if (event.type == bw::core::ScriptLogEventType::Error) {
          logger->error("Lua [" + event.scriptName + "]: " + event.message);
        }
      });
  bw::core::registerScriptStepTypes(*scriptRuntime);

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
  resourceMgr->addResourceFactory(
      new MapResourceFactory(logger, *scriptRuntime));
  resourceMgr->addResourceFactory(new ProtoEntityResourceFactory(model->entityHandler, model->animationDatabase));
  bw::core::registerLuaScriptResourceType(*resourceMgr);
  resourceMgr->addResourceFactory(new ProcMaterialResourceFactory());
  resourceMgr->addResourceFactory(new TriplanarMaterialResourceFactory());
  resourceMgr->addResourceFactory(new EmbossingCatalogResourceFactory());
  resourceMgr->addResourceFactory(new AcousticCatalogResourceFactory());

  // Add resource definition factories
  resourceMgr->addResourceDefinitionFactory(new MapBooleanWorldDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new applib::MapTiledDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new ProtoEntityDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new applib::ImageSetTiledDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(new ProcMaterialResourceDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(
      new TriplanarMaterialResourceDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(
      new EmbossingCatalogResourceDefinitionFactory());
  resourceMgr->addResourceDefinitionFactory(
      new AcousticCatalogResourceDefinitionFactory());
}

APPLICATION_API void dllOnExit() {
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

  scriptRuntime.reset();
}
}