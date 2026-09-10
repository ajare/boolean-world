#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <glm/geometric.hpp>

#include <fmod.hpp>
#include <fmod_errors.h>
#include <fmod_studio.hpp>
#include <phonon.h>

#include <willpower/application/AudioSystem.h>

#include "AcousticPresetResolver.h"
#include "AcousticScene.h"
#include "AudioSimulationOptions.h"
#include "EmitterSourcePolicy.h"
#include "PeriodicSnapshotWorker.h"
#include "PlayerView.h"
#include "SteamAudio.h"

#include <core/ArrangementWorldData.h>

namespace {

using InitializeFunction = void(IPLCALL*)(IPLContext);
using TerminateFunction = void(IPLCALL*)();
using SetHRTFFunction = void(IPLCALL*)(IPLHRTF);
using SetSimulationSettingsFunction = void(IPLCALL*)(IPLSimulationSettings);
using ContextReleaseFunction = void(IPLCALL*)(IPLContext*);
using HRTFReleaseFunction = void(IPLCALL*)(IPLHRTF*);
using ContextRetainFunction = IPLContext(IPLCALL*)(IPLContext);
using SceneCreateFunction = IPLerror(IPLCALL*)(
    IPLContext, IPLSceneSettings*, IPLScene*);
using SceneReleaseFunction = void(IPLCALL*)(IPLScene*);
using SceneCommitFunction = void(IPLCALL*)(IPLScene);
using StaticMeshCreateFunction = IPLerror(IPLCALL*)(
    IPLScene, IPLStaticMeshSettings*, IPLStaticMesh*);
using StaticMeshReleaseFunction = void(IPLCALL*)(IPLStaticMesh*);
using StaticMeshAddFunction = void(IPLCALL*)(IPLStaticMesh, IPLScene);
using SimulatorCreateFunction = IPLerror(IPLCALL*)(
    IPLContext, IPLSimulationSettings*, IPLSimulator*);
using SimulatorReleaseFunction = void(IPLCALL*)(IPLSimulator*);
using SimulatorSetSceneFunction = void(IPLCALL*)(IPLSimulator, IPLScene);
using SimulatorCommitFunction = void(IPLCALL*)(IPLSimulator);
using SimulatorSetSharedInputsFunction = void(IPLCALL*)(
    IPLSimulator, IPLSimulationFlags, IPLSimulationSharedInputs*);
using SimulatorRunDirectFunction = void(IPLCALL*)(IPLSimulator);
using SimulatorRunReflectionsFunction = void(IPLCALL*)(IPLSimulator);
using SourceCreateFunction = IPLerror(IPLCALL*)(
    IPLSimulator, IPLSourceSettings*, IPLSource*);
using SourceReleaseFunction = void(IPLCALL*)(IPLSource*);
using SourceAddFunction = void(IPLCALL*)(IPLSource, IPLSimulator);
using SourceRemoveFunction = void(IPLCALL*)(IPLSource, IPLSimulator);
using SourceSetInputsFunction = void(IPLCALL*)(
    IPLSource, IPLSimulationFlags, IPLSimulationInputs*);
using FmodAddSourceFunction = IPLint32(IPLCALL*)(IPLSource);
using FmodRemoveSourceFunction = void(IPLCALL*)(IPLint32);

constexpr IPLfloat32 TopPresetIrradianceMinDistance = 1.0f;
constexpr float ReflectionHysteresisMargin = 0.1f;
constexpr float ReflectionFadeDuration = 0.3f;
constexpr float VanishedEmitterFadeDuration = 0.3f;

// Steam Audio 4.8.1's FMOD Spatializer parameter indices. The integration's
// public C header is not shipped with the binary SDK, so keep only the three
// runtime-controlled parameters here.
constexpr int ApplyReflectionsParameter = 7;
constexpr int ReflectionsMixLevelParameter = 27;
constexpr int SimulationOutputsHandleParameter = 33;
constexpr char SteamAudioSpatializerName[] = "Steam Audio Spatializer";

#if defined(_WIN32)
using NativeModule = HMODULE;
constexpr char SteamAudioCoreLibrary[] = "phonon.dll";
constexpr char SteamAudioFmodLibrary[] = "phonon_fmod.dll";

NativeModule findNativeModule(char const* name) {
  return GetModuleHandleA(name);
}

NativeModule loadNativeModule(char const* name) {
  if (auto module = findNativeModule(name)) return module;
  return LoadLibraryA(name);
}

void* nativeFunction(NativeModule module, char const* name) {
  return reinterpret_cast<void*>(GetProcAddress(module, name));
}
#else
using NativeModule = void*;
constexpr char SteamAudioCoreLibrary[] = "libphonon.so";
constexpr char SteamAudioFmodLibrary[] = "libphonon_fmod.so";

NativeModule findNativeModule(char const* name) {
  return dlopen(name, RTLD_NOW | RTLD_NOLOAD);
}

NativeModule loadNativeModule(char const* name) {
  if (auto module = findNativeModule(name)) return module;
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void* nativeFunction(NativeModule module, char const* name) {
  return dlsym(module, name);
}
#endif

void* steamAudioFunction(NativeModule module, char const* name) {
  auto* function = nativeFunction(module, name);
  if (!function) {
    throw std::runtime_error(
        std::string("Steam Audio library is missing required API: ") + name);
  }
  return function;
}

FMOD_VECTOR fmodVector(glm::vec3 const& value) {
  return {value.x, value.y, value.z};
}

IPLVector3 iplVector(glm::vec3 const& value) {
  return {value.x, value.y, value.z};
}

IPLCoordinateSpace3 sourceCoordinates(glm::vec3 const& position) {
  return {{1.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f},
          {0.0f, 0.0f, -1.0f},
          iplVector(position)};
}

IPLSimulationInputs sourceSimulationInputs(
    IPLCoordinateSpace3 const& coordinates, IPLSimulationFlags flags,
    bw::app::AudioFeatureOptions const& features) {
  IPLSimulationInputs inputs{};
  inputs.flags = flags;
  auto directFlags = 0;
  if (features.airAbsorption) {
    directFlags |= IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION;
  }
  if (features.occlusion) directFlags |= IPL_DIRECTSIMULATIONFLAGS_OCCLUSION;
  if (features.transmission) {
    directFlags |= IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION;
  }
  inputs.directFlags = static_cast<IPLDirectSimulationFlags>(directFlags);
  inputs.source = coordinates;
  inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
  inputs.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
  inputs.numOcclusionSamples = 1;
  inputs.reverbScale[0] = 1.0f;
  inputs.reverbScale[1] = 1.0f;
  inputs.reverbScale[2] = 1.0f;
  inputs.hybridReverbTransitionTime = 0.1f;
  inputs.hybridReverbOverlapPercent = 0.25f;
  inputs.numTransmissionRays = 1;
  return inputs;
}

bw::app::EmitterSourceIdentity emitterIdentity(
    bw::core::CapturedAudioEmitter const& emitter) {
  std::optional<bw::app::EmitterSourcePlacement> placement;
  if (emitter.placementKey) {
    placement = bw::app::EmitterSourcePlacement{
        emitter.placementKey->tileX, emitter.placementKey->tileY,
        emitter.placementKey->gridSize};
  }
  return {emitter.guid, placement};
}

bool sameEmitterIdentity(
    bw::core::CapturedAudioEmitter const& lhs,
    bw::core::CapturedAudioEmitter const& rhs) {
  return emitterIdentity(lhs) == emitterIdentity(rhs);
}

std::size_t emitterRankingKey(
    bw::core::CapturedAudioEmitter const& emitter) {
  // FNV-1a gives equal-distance sources a generation-order-independent tie
  // break. Hysteresis handles ordinary movement; this handles first entry.
  uint64_t hash = 14695981039346656037ull;
  auto append = [&hash](uint32_t value) {
    for (auto byte = 0; byte < 4; ++byte) {
      hash ^= (value >> (byte * 8)) & 0xffu;
      hash *= 1099511628211ull;
    }
  };
  for (auto character : emitter.guid) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ull;
  }
  append(emitter.placementKey ? 1u : 0u);
  if (emitter.placementKey) {
    append(static_cast<uint32_t>(emitter.placementKey->tileX));
    append(static_cast<uint32_t>(emitter.placementKey->tileY));
    append(emitter.placementKey->gridSize);
  }
  return static_cast<std::size_t>(hash);
}

#if defined(WP_APPLICATION_USE_FMOD)
void requireFmod(FMOD_RESULT result, char const* operation) {
  if (result != FMOD_OK) {
    throw std::runtime_error(
        std::string(operation) + ": " + FMOD_ErrorString(result));
  }
}
#endif

}  // namespace

namespace bw::app {

struct AcousticScene::Implementation {
  IPLContext context{};
  IPLScene scene{};
  IPLStaticMesh staticMesh{};
  ContextReleaseFunction contextRelease{};
  SceneReleaseFunction sceneRelease{};
  StaticMeshReleaseFunction staticMeshRelease{};

  ~Implementation() {
    if (staticMesh) staticMeshRelease(&staticMesh);
    if (scene) sceneRelease(&scene);
    if (context) contextRelease(&context);
  }
};

AcousticScene::AcousticScene(
    core::ArrangementWorldDataPtr sourceWorld,
    AcousticSceneMesh mesh,
    std::unique_ptr<Implementation> implementation)
    : mSourceWorld(std::move(sourceWorld)),
      mMesh(std::move(mesh)),
      mImplementation(std::move(implementation)) {}

AcousticScene::~AcousticScene() = default;

core::ArrangementWorldDataPtr const& AcousticScene::getSourceWorld() const {
  return mSourceWorld;
}

AcousticSceneMesh const& AcousticScene::getExportedMesh() const {
  return mMesh;
}

struct SteamAudio::Implementation {
  struct ListenerState {
    IPLCoordinateSpace3 coordinates{};
  };

  struct NativeSource {
    IPLSource source{};
    SourceReleaseFunction release{};

    ~NativeSource() {
      if (source) release(&source);
    }
  };

  struct ReflectionSourceState {
    std::shared_ptr<NativeSource> source;
    IPLCoordinateSpace3 coordinates{};
    bool selected{};
  };

  struct PublishedSimulationState {
    AcousticScenePtr scene;
    uint64_t sceneVersion{};
    AudioQualityPreset quality;
    AudioFeatureOptions features;
    std::vector<ReflectionSourceState> sources;
  };

  struct ActiveEmitter {
    core::CapturedAudioEmitter definition;
    FMOD::Studio::EventInstance* event{};
    FMOD::DSP* spatializer{};
    std::shared_ptr<NativeSource> source;
    IPLint32 fmodSourceHandle{-1};
    float authoredReflectionMix{1.0f};
    float reflectionContribution{0.0f};
    float volume{1.0f};
    bool selectedForReflections{false};
    bool present{true};
  };

  wp::application::AudioSystem* audioSystem{};
  FMOD::System* coreSystem{};
  IPLContext context{};
  IPLHRTF hrtf{};
  IPLSimulator simulator{};
  TerminateFunction terminate{};
  bool fmodInitialized{false};
  ContextReleaseFunction contextRelease{};
  HRTFReleaseFunction hrtfRelease{};
  ContextRetainFunction contextRetain{};
  SceneCreateFunction sceneCreate{};
  SceneReleaseFunction sceneRelease{};
  SceneCommitFunction sceneCommit{};
  StaticMeshCreateFunction staticMeshCreate{};
  StaticMeshReleaseFunction staticMeshRelease{};
  StaticMeshAddFunction staticMeshAdd{};
  SimulatorReleaseFunction simulatorRelease{};
  SimulatorSetSceneFunction simulatorSetScene{};
  SimulatorCommitFunction simulatorCommit{};
  SimulatorSetSharedInputsFunction simulatorSetSharedInputs{};
  SimulatorRunDirectFunction simulatorRunDirect{};
  SimulatorRunReflectionsFunction simulatorRunReflections{};
  SourceCreateFunction sourceCreate{};
  SourceReleaseFunction sourceRelease{};
  SourceAddFunction sourceAdd{};
  SourceRemoveFunction sourceRemove{};
  SourceSetInputsFunction sourceSetInputs{};
  FmodAddSourceFunction fmodAddSource{};
  FmodRemoveSourceFunction fmodRemoveSource{};

  std::atomic<std::shared_ptr<ListenerState const>> listener;
  std::shared_mutex sceneTransitionMutex;
  std::atomic<bool> sceneReady{false};
  uint64_t simulationSceneVersion{};
  uint64_t publishedSceneVersion{};
  uint64_t nextSceneVersion{1};
  core::ArrangementWorldDataPtr publishedWorld;
  AcousticScenePtr publishedScene;
  AudioSimulationOptions options;
  AudioQualityPreset quality;
  std::vector<core::CapturedAudioEmitter> emitterDefinitions;
  std::vector<ActiveEmitter> activeEmitters;
  AudioSimulationDiagnostics diagnostics;
  std::atomic<std::int64_t> previousReflectionTickStartedNs{};
  std::atomic<std::uint64_t> reflectionThreadCostNs{};
  std::atomic<double> reflectionUpdateRateHz{};
  std::unique_ptr<PeriodicSnapshotWorker> reflectionWorker;

  void publishSimulationState() {
    if (!reflectionWorker || !publishedScene) return;

    auto state = std::make_shared<PublishedSimulationState>();
    state->scene = publishedScene;
    state->sceneVersion = publishedSceneVersion;
    state->quality = quality;
    state->features = options.features;
    state->sources.reserve(activeEmitters.size());
    for (auto const& emitter : activeEmitters) {
      state->sources.push_back(
          {emitter.source,
           sourceCoordinates(worldToRendererAudioPosition(
               emitter.definition.position, emitter.definition.height)),
           emitter.present && emitter.selectedForReflections});
    }
    reflectionWorker->publish(std::move(state));
  }

  void removeNativeSource(ActiveEmitter& emitter) {
    if (emitter.fmodSourceHandle >= 0) {
      fmodRemoveSource(emitter.fmodSourceHandle);
      emitter.fmodSourceHandle = -1;
    }
    if (emitter.source && emitter.source->source) {
      sourceRemove(emitter.source->source, simulator);
    }
  }

  void stopEvent(ActiveEmitter& emitter, FMOD_STUDIO_STOP_MODE mode) {
    if (emitter.event) {
      emitter.event->stop(mode);
      emitter.event->release();
      emitter.event = nullptr;
      emitter.spatializer = nullptr;
    }
  }

  ~Implementation() {
    // The worker and every scene/source snapshot it can retain must be gone
    // before sources, the simulator, context, and loaded API are released.
    reflectionWorker.reset();
#if defined(WP_APPLICATION_USE_FMOD)
    if (simulator && !activeEmitters.empty()) {
      for (auto& emitter : activeEmitters) {
        stopEvent(emitter, FMOD_STUDIO_STOP_IMMEDIATE);
        removeNativeSource(emitter);
      }
      simulatorCommit(simulator);
      activeEmitters.clear();
      if (audioSystem) audioSystem->update();
    }
#endif
    publishedScene.reset();
    if (simulator) simulatorRelease(&simulator);
    if (fmodInitialized) terminate();
    if (hrtf) hrtfRelease(&hrtf);
    if (context) contextRelease(&context);
  }
};

void SteamAudio::loadPlugin(wp::application::AudioSystem& audioSystem) {
#if defined(WP_APPLICATION_USE_FMOD)
  auto* coreSystem = audioSystem.getCoreSystem();
  if (!coreSystem) {
    throw std::runtime_error("FMOD core system is unavailable");
  }

  // The FMOD integration does not load the Steam Audio core until its DSP is
  // instantiated, which is too late for us to resolve the simulation API. Keep
  // it process-loaded alongside the FMOD-owned plugin.
  if (!loadNativeModule(SteamAudioCoreLibrary)) {
    throw std::runtime_error("Unable to load Steam Audio core library");
  }
  if (!findNativeModule(SteamAudioFmodLibrary)) {
    unsigned int pluginHandle{};
#if defined(_WIN32)
    auto const* pluginPath = SteamAudioFmodLibrary;
#else
    // FMOD performs the dlopen itself, so libBooleanWorld's $ORIGIN RUNPATH
    // does not participate. Point it at the plugin staged beside Launcher.
    auto const stagedPlugin =
        std::filesystem::absolute(SteamAudioFmodLibrary).string();
    auto const* pluginPath = stagedPlugin.c_str();
#endif
    requireFmod(coreSystem->loadPlugin(pluginPath, &pluginHandle, 0),
                "Unable to load the Steam Audio FMOD plug-in");
  }
#else
  (void)audioSystem;
#endif
}

SteamAudio::SteamAudio(
    wp::application::AudioSystem& audioSystem,
    AudioSimulationOptions const& options)
    : mImplementation(new Implementation) {
  auto& implementation = *mImplementation;
  implementation.options = options;
  auto selected = implementation.options.findPreset(
      implementation.options.qualityPreset);
  if (!selected || implementation.options.presets.empty()) {
    throw std::invalid_argument("Audio quality preset is not configured");
  }
  if (!implementation.options.features.occlusion &&
      implementation.options.features.transmission) {
    throw std::invalid_argument(
        "Audio transmission requires occlusion to be enabled");
  }
  implementation.quality = *selected;
#if defined(WP_APPLICATION_USE_FMOD)
  implementation.audioSystem = &audioSystem;
  loadPlugin(audioSystem);
  implementation.coreSystem = audioSystem.getCoreSystem();

  // loadPlugin registered the FMOD integration and process-loaded the core
  // before resolving either library's API entry points.
  auto pluginModule = loadNativeModule(SteamAudioFmodLibrary);
  if (!pluginModule) {
    throw std::runtime_error("FMOD loaded Steam Audio without its library");
  }
  auto phononModule = loadNativeModule(SteamAudioCoreLibrary);
  if (!phononModule) {
    throw std::runtime_error("Steam Audio core library is unavailable");
  }

  auto contextCreate = reinterpret_cast<decltype(&iplContextCreate)>(
      steamAudioFunction(phononModule, "iplContextCreate"));
  auto hrtfCreate = reinterpret_cast<decltype(&iplHRTFCreate)>(
      steamAudioFunction(phononModule, "iplHRTFCreate"));
  implementation.contextRelease = reinterpret_cast<ContextReleaseFunction>(
      steamAudioFunction(phononModule, "iplContextRelease"));
  implementation.hrtfRelease = reinterpret_cast<HRTFReleaseFunction>(
      steamAudioFunction(phononModule, "iplHRTFRelease"));
  implementation.contextRetain = reinterpret_cast<ContextRetainFunction>(
      steamAudioFunction(phononModule, "iplContextRetain"));
  implementation.sceneCreate = reinterpret_cast<SceneCreateFunction>(
      steamAudioFunction(phononModule, "iplSceneCreate"));
  implementation.sceneRelease = reinterpret_cast<SceneReleaseFunction>(
      steamAudioFunction(phononModule, "iplSceneRelease"));
  implementation.sceneCommit = reinterpret_cast<SceneCommitFunction>(
      steamAudioFunction(phononModule, "iplSceneCommit"));
  implementation.staticMeshCreate =
      reinterpret_cast<StaticMeshCreateFunction>(
          steamAudioFunction(phononModule, "iplStaticMeshCreate"));
  implementation.staticMeshRelease =
      reinterpret_cast<StaticMeshReleaseFunction>(
          steamAudioFunction(phononModule, "iplStaticMeshRelease"));
  implementation.staticMeshAdd = reinterpret_cast<StaticMeshAddFunction>(
      steamAudioFunction(phononModule, "iplStaticMeshAdd"));
  auto simulatorCreate = reinterpret_cast<SimulatorCreateFunction>(
      steamAudioFunction(phononModule, "iplSimulatorCreate"));
  implementation.simulatorRelease =
      reinterpret_cast<SimulatorReleaseFunction>(
          steamAudioFunction(phononModule, "iplSimulatorRelease"));
  implementation.simulatorSetScene =
      reinterpret_cast<SimulatorSetSceneFunction>(
          steamAudioFunction(phononModule, "iplSimulatorSetScene"));
  implementation.simulatorCommit =
      reinterpret_cast<SimulatorCommitFunction>(
          steamAudioFunction(phononModule, "iplSimulatorCommit"));
  implementation.simulatorSetSharedInputs =
      reinterpret_cast<SimulatorSetSharedInputsFunction>(
          steamAudioFunction(phononModule, "iplSimulatorSetSharedInputs"));
  implementation.simulatorRunDirect =
      reinterpret_cast<SimulatorRunDirectFunction>(
          steamAudioFunction(phononModule, "iplSimulatorRunDirect"));
  implementation.simulatorRunReflections =
      reinterpret_cast<SimulatorRunReflectionsFunction>(
          steamAudioFunction(phononModule, "iplSimulatorRunReflections"));
  implementation.sourceCreate = reinterpret_cast<SourceCreateFunction>(
      steamAudioFunction(phononModule, "iplSourceCreate"));
  implementation.sourceAdd = reinterpret_cast<SourceAddFunction>(
      steamAudioFunction(phononModule, "iplSourceAdd"));
  implementation.sourceRelease = reinterpret_cast<SourceReleaseFunction>(
      steamAudioFunction(phononModule, "iplSourceRelease"));
  implementation.sourceRemove = reinterpret_cast<SourceRemoveFunction>(
      steamAudioFunction(phononModule, "iplSourceRemove"));
  implementation.sourceSetInputs =
      reinterpret_cast<SourceSetInputsFunction>(
          steamAudioFunction(phononModule, "iplSourceSetInputs"));
  implementation.fmodAddSource = reinterpret_cast<FmodAddSourceFunction>(
      steamAudioFunction(pluginModule, "iplFMODAddSource"));
  implementation.fmodRemoveSource =
      reinterpret_cast<FmodRemoveSourceFunction>(
          steamAudioFunction(pluginModule, "iplFMODRemoveSource"));
  implementation.terminate = reinterpret_cast<TerminateFunction>(
      steamAudioFunction(pluginModule, "iplFMODTerminate"));
  auto initialize = reinterpret_cast<InitializeFunction>(
      steamAudioFunction(pluginModule, "iplFMODInitialize"));
  auto setHrtf = reinterpret_cast<SetHRTFFunction>(
      steamAudioFunction(pluginModule, "iplFMODSetHRTF"));
  auto setSimulationSettings = reinterpret_cast<SetSimulationSettingsFunction>(
      steamAudioFunction(pluginModule, "iplFMODSetSimulationSettings"));

  IPLContextSettings contextSettings{};
  contextSettings.version = STEAMAUDIO_VERSION;
  contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
  if (contextCreate(&contextSettings, &implementation.context) !=
      IPL_STATUS_SUCCESS) {
    throw std::runtime_error("Unable to create Steam Audio context");
  }

  int sampleRate{};
  if (implementation.coreSystem->getSoftwareFormat(&sampleRate, nullptr,
                                                   nullptr) != FMOD_OK) {
    throw std::runtime_error("Unable to query FMOD audio format");
  }
  unsigned int frameSize{};
  if (implementation.coreSystem->getDSPBufferSize(&frameSize, nullptr) !=
      FMOD_OK) {
    throw std::runtime_error("Unable to query FMOD DSP buffer size");
  }
  IPLAudioSettings audioSettings{sampleRate, static_cast<IPLint32>(frameSize)};

  // The plugin must see its context before it receives the HRTF and simulator.
  initialize(implementation.context);
  implementation.fmodInitialized = true;

  IPLHRTFSettings hrtfSettings{};
  hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
  hrtfSettings.volume = 1.0f;
  if (hrtfCreate(implementation.context, &audioSettings, &hrtfSettings,
                 &implementation.hrtf) != IPL_STATUS_SUCCESS) {
    throw std::runtime_error("Unable to create Steam Audio HRTF");
  }
  setHrtf(implementation.hrtf);

  IPLSimulationSettings simulationSettings{};
  simulationSettings.flags = static_cast<IPLSimulationFlags>(
      IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
  simulationSettings.sceneType = IPL_SCENETYPE_DEFAULT;
  simulationSettings.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
  simulationSettings.maxNumOcclusionSamples = 1;
  auto maximum = [&options](auto member) {
    auto value = options.presets.front().*member;
    for (auto const& preset : options.presets) {
      value = std::max(value, preset.*member);
    }
    return value;
  };
  auto const maxRays = maximum(&AudioQualityPreset::rayCount);
  auto const maxDuration = maximum(
      &AudioQualityPreset::impulseResponseDuration);
  auto const maxOrder = maximum(&AudioQualityPreset::ambisonicOrder);
  auto const maxSources = maximum(&AudioQualityPreset::reflectionSourceCap);
  if (maxRays > std::uint32_t(std::numeric_limits<IPLint32>::max()) ||
      maxOrder > std::uint32_t(std::numeric_limits<IPLint32>::max()) ||
      maxSources > std::uint32_t(std::numeric_limits<IPLint32>::max())) {
    throw std::overflow_error("Audio quality preset exceeds Steam Audio limits");
  }
  simulationSettings.maxNumRays = static_cast<IPLint32>(maxRays);
  simulationSettings.numDiffuseSamples = 1;
  simulationSettings.maxDuration = maxDuration;
  simulationSettings.maxOrder = static_cast<IPLint32>(maxOrder);
  simulationSettings.maxNumSources = static_cast<IPLint32>(maxSources);
  simulationSettings.numThreads = 1;
  simulationSettings.rayBatchSize = 1;
  simulationSettings.samplingRate = sampleRate;
  simulationSettings.frameSize = static_cast<IPLint32>(frameSize);
  setSimulationSettings(simulationSettings);

  if (simulatorCreate(implementation.context, &simulationSettings,
                      &implementation.simulator) != IPL_STATUS_SUCCESS) {
    throw std::runtime_error("Unable to create Steam Audio simulator");
  }

  implementation.reflectionWorker =
      std::make_unique<PeriodicSnapshotWorker>(
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<float>(
                  1.0f / implementation.quality.simulationUpdateRate)),
          [&implementation](PeriodicSnapshotWorker::SnapshotPtr const& value) {
            auto published = std::static_pointer_cast<
                Implementation::PublishedSimulationState const>(value);
            auto listener = implementation.listener.load(
                std::memory_order_acquire);
            if (!published || !published->scene || !listener ||
                !published->scene->mImplementation) {
              return;
            }

            auto const tickStarted = std::chrono::steady_clock::now();
            auto const tickStartedNs = std::chrono::duration_cast<
                                           std::chrono::nanoseconds>(tickStarted.time_since_epoch())
                                           .count();
            auto const previousStartedNs =
                implementation.previousReflectionTickStartedNs.exchange(
                    tickStartedNs, std::memory_order_relaxed);
            if (previousStartedNs > 0 && tickStartedNs > previousStartedNs) {
              implementation.reflectionUpdateRateHz.store(
                  1'000'000'000.0 /
                      static_cast<double>(tickStartedNs - previousStartedNs),
                  std::memory_order_relaxed);
            }

            if (published->sceneVersion !=
                implementation.simulationSceneVersion) {
              std::unique_lock transition(
                  implementation.sceneTransitionMutex);
              implementation.simulatorSetScene(
                  implementation.simulator,
                  published->scene->mImplementation->scene);
              implementation.simulatorCommit(implementation.simulator);
              implementation.simulationSceneVersion =
                  published->sceneVersion;
              implementation.sceneReady.store(
                  true, std::memory_order_release);
            }

            IPLSimulationSharedInputs inputs{};
            inputs.listener = listener->coordinates;
            inputs.numRays = static_cast<IPLint32>(published->quality.rayCount);
            inputs.numBounces = static_cast<IPLint32>(published->quality.bounceCount);
            inputs.duration = published->quality.impulseResponseDuration;
            inputs.order = static_cast<IPLint32>(published->quality.ambisonicOrder);
            inputs.irradianceMinDistance = TopPresetIrradianceMinDistance;

            std::shared_lock simulation(
                implementation.sceneTransitionMutex);
            implementation.simulatorSetSharedInputs(
                implementation.simulator,
                IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
            for (auto const& source : published->sources) {
              auto sourceInputs = sourceSimulationInputs(
                  source.coordinates,
                  source.selected && published->features.reflections
                      ? IPL_SIMULATIONFLAGS_REFLECTIONS
                      : static_cast<IPLSimulationFlags>(0),
                  published->features);
              implementation.sourceSetInputs(
                  source.source->source, IPL_SIMULATIONFLAGS_REFLECTIONS,
                  &sourceInputs);
            }
            if (published->features.reflections) {
              implementation.simulatorRunReflections(
                  implementation.simulator);
            }
            implementation.reflectionThreadCostNs.store(
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - tickStarted)
                        .count()),
                std::memory_order_relaxed);
          });
#else
  (void)audioSystem;
#endif
}

SteamAudio::~SteamAudio() {
  delete mImplementation;
}

bool SteamAudio::setQualityPreset(std::string_view name) {
  auto& implementation = *mImplementation;
  auto preset = implementation.options.findPreset(name);
  if (!preset) return false;
  if (!implementation.options.qualityMayBeModifiedLive &&
      name != implementation.quality.name) {
    return false;
  }
  implementation.quality = *preset;
  implementation.options.qualityPreset = preset->name;
  if (implementation.reflectionWorker) {
    implementation.reflectionWorker->setInterval(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(
                1.0f / preset->simulationUpdateRate)));
    implementation.publishSimulationState();
  }
  return true;
}

std::string_view SteamAudio::getQualityPreset() const {
  return mImplementation->quality.name;
}

bool SteamAudio::qualityMayBeModifiedLive() const {
  return mImplementation->options.qualityMayBeModifiedLive;
}

std::vector<AudioQualityPreset> const& SteamAudio::getQualityPresets() const {
  return mImplementation->options.presets;
}

AudioQualityPreset const& SteamAudio::getQualitySettings() const {
  return mImplementation->quality;
}

AudioFeatureOptions SteamAudio::getFeatures() const {
  return mImplementation->options.features;
}

bool SteamAudio::setFeatures(AudioFeatureOptions features) {
  if (!features.occlusion && features.transmission) return false;
  mImplementation->options.features = features;
  mImplementation->publishSimulationState();
  return true;
}

AudioSimulationDiagnostics SteamAudio::getDiagnostics() const {
  auto result = mImplementation->diagnostics;
  result.reflectionThreadCostMilliseconds =
      static_cast<double>(mImplementation->reflectionThreadCostNs.load(
          std::memory_order_relaxed)) /
      1'000'000.0;
  result.reflectionUpdateRateHz =
      mImplementation->reflectionUpdateRateHz.load(std::memory_order_relaxed);
  return result;
}

AcousticScenePtr SteamAudio::buildScene(
    core::ArrangementWorldDataPtr sourceWorld,
    AcousticPresetResolver const& resolver) const {
  if (!sourceWorld) {
    throw std::invalid_argument("Acoustic scene requires a World snapshot");
  }

  auto mesh = ExportAcousticSceneMesh(
      *sourceWorld,
      [&](core::SurfaceMaterialReference const& material)
          -> AcousticPreset const& {
        return resolver.resolveSurfaceMaterial(material);
      });

  std::unique_ptr<AcousticScene::Implementation> native;
#if defined(WP_APPLICATION_USE_FMOD)
  auto fitsIplInt = [](size_t size) {
    return size <= size_t(std::numeric_limits<IPLint32>::max());
  };
  if (!fitsIplInt(mesh.vertices.size()) ||
      !fitsIplInt(mesh.triangles.size()) ||
      !fitsIplInt(mesh.materials.size())) {
    throw std::overflow_error("Acoustic scene exceeds Steam Audio limits");
  }

  auto const& api = *mImplementation;
  native = std::make_unique<AcousticScene::Implementation>();
  native->contextRelease = api.contextRelease;
  native->sceneRelease = api.sceneRelease;
  native->staticMeshRelease = api.staticMeshRelease;
  native->context = api.contextRetain(api.context);

  IPLSceneSettings sceneSettings{};
  sceneSettings.type = IPL_SCENETYPE_DEFAULT;
  if (api.sceneCreate(native->context, &sceneSettings, &native->scene) !=
      IPL_STATUS_SUCCESS) {
    throw std::runtime_error("Unable to create Steam Audio scene");
  }

  if (!mesh.triangles.empty()) {
    std::vector<IPLVector3> vertices;
    vertices.reserve(mesh.vertices.size());
    for (auto const& vertex : mesh.vertices) {
      vertices.push_back({vertex.x, vertex.y, vertex.z});
    }
    std::vector<IPLTriangle> triangles;
    std::vector<IPLint32> materialIndices;
    triangles.reserve(mesh.triangles.size());
    materialIndices.reserve(mesh.triangles.size());
    for (auto const& triangle : mesh.triangles) {
      triangles.push_back(
          {{IPLint32(triangle.vertices[0]), IPLint32(triangle.vertices[1]),
            IPLint32(triangle.vertices[2])}});
      materialIndices.push_back(IPLint32(triangle.materialIndex));
    }
    std::vector<IPLMaterial> materials;
    materials.reserve(mesh.materials.size());
    for (auto const& preset : mesh.materials) {
      materials.push_back(
          {{preset.absorption[0], preset.absorption[1], preset.absorption[2]},
           preset.scattering,
           {preset.transmission[0], preset.transmission[1],
            preset.transmission[2]}});
    }

    IPLStaticMeshSettings meshSettings{
        IPLint32(vertices.size()), IPLint32(triangles.size()),
        IPLint32(materials.size()), vertices.data(), triangles.data(),
        materialIndices.data(), materials.data()};
    if (api.staticMeshCreate(
            native->scene, &meshSettings, &native->staticMesh) !=
        IPL_STATUS_SUCCESS) {
      throw std::runtime_error("Unable to create Steam Audio static mesh");
    }
    api.staticMeshAdd(native->staticMesh, native->scene);
  }
  api.sceneCommit(native->scene);
#endif

  return std::shared_ptr<AcousticScene const>(new AcousticScene(
      std::move(sourceWorld), std::move(mesh), std::move(native)));
}

void SteamAudio::updateWorldSnapshot(
    core::ArrangementWorldDataPtr sourceWorld,
    AcousticPresetResolver const& resolver) {
  if (!sourceWorld) {
    throw std::invalid_argument("Acoustic simulation requires a World snapshot");
  }
  if (sourceWorld == mImplementation->publishedWorld) return;

  // buildScene does all native scene construction and commit work before the
  // pair becomes visible. The worker can therefore only load complete pairs.
  auto scene = buildScene(sourceWorld, resolver);
#if defined(WP_APPLICATION_USE_FMOD)
  mImplementation->publishedScene = std::move(scene);
  mImplementation->publishedSceneVersion =
      mImplementation->nextSceneVersion++;
  mImplementation->publishSimulationState();
#endif
  mImplementation->publishedWorld = std::move(sourceWorld);
}

void SteamAudio::syncEmitters(core::ArrangementWorldDataPtr sourceWorld) {
  if (!sourceWorld) {
    throw std::invalid_argument("Emitter sync requires a World snapshot");
  }
  auto& implementation = *mImplementation;
  auto const& next = sourceWorld->getCapturedAudioEmitters();
  implementation.diagnostics.capturedEmitterCount = next.size();
#if defined(WP_APPLICATION_USE_FMOD)
  std::vector<EmitterSourceIdentity> previousIdentities;
  std::vector<EmitterSourceIdentity> nextIdentities;
  previousIdentities.reserve(implementation.activeEmitters.size());
  nextIdentities.reserve(next.size());
  for (auto const& active : implementation.activeEmitters) {
    previousIdentities.push_back(emitterIdentity(active.definition));
  }
  for (auto const& emitter : next) {
    nextIdentities.push_back(emitterIdentity(emitter));
  }
  auto reconciliation = reconcileEmitterSources(
      previousIdentities, nextIdentities);

  for (auto& active : implementation.activeEmitters) {
    active.present = false;
    active.selectedForReflections = false;
  }
  for (auto const& [activeIndex, nextIndex] : reconciliation.retained) {
    auto& active = implementation.activeEmitters[activeIndex];
    // In particular, do not touch active.event: preserving this pointer is
    // what keeps a looping event's timeline continuous across commits.
    active.definition = next[nextIndex];
    active.present = true;
  }
  implementation.emitterDefinitions.assign(next.begin(), next.end());
  implementation.publishSimulationState();
#else
  implementation.emitterDefinitions.assign(next.begin(), next.end());
#endif
}

void SteamAudio::updateEmitters(
    glm::vec3 const& listenerPosition, float frameTime) {
#if defined(WP_APPLICATION_USE_FMOD)
  auto& implementation = *mImplementation;
  frameTime = std::isfinite(frameTime) ? std::max(frameTime, 0.0f) : 0.0f;

  auto sourcePosition = [](core::CapturedAudioEmitter const& emitter) {
    return worldToRendererAudioPosition(emitter.position, emitter.height);
  };
  auto findActive = [&implementation](
                        core::CapturedAudioEmitter const& definition) {
    return std::ranges::find_if(
        implementation.activeEmitters,
        [&definition](Implementation::ActiveEmitter const& active) {
          return active.present &&
                 sameEmitterIdentity(active.definition, definition) &&
                 active.definition.soundId == definition.soundId;
        });
  };

  std::vector<EmitterSourceCandidate> candidates;
  candidates.reserve(implementation.emitterDefinitions.size());
  for (std::size_t i = 0; i < implementation.emitterDefinitions.size(); ++i) {
    auto const& definition = implementation.emitterDefinitions[i];
    auto active = findActive(definition);
    candidates.push_back(
        {emitterRankingKey(definition),
         glm::distance(sourcePosition(definition), listenerPosition),
         definition.cullRadius,
         active != implementation.activeEmitters.end() &&
             active->selectedForReflections});
  }
  auto decisions = selectEmitterSources(
      candidates,
      implementation.options.features.reflections
          ? implementation.quality.reflectionSourceCap
          : 0,
      ReflectionHysteresisMargin);

  implementation.diagnostics.inCullRangeEmitterCount = 0;
  implementation.diagnostics.reflectionEmitterCount = 0;
  implementation.diagnostics.rankedEmitters.clear();
  implementation.diagnostics.rankedEmitters.reserve(decisions.size());
  for (std::size_t i = 0; i < decisions.size(); ++i) {
    auto const& decision = decisions[i];
    if (!decision.inRange) continue;
    ++implementation.diagnostics.inCullRangeEmitterCount;
    if (decision.selectedForReflections) {
      ++implementation.diagnostics.reflectionEmitterCount;
    }
    auto const& emitter = implementation.emitterDefinitions[i];
    implementation.diagnostics.rankedEmitters.push_back(
        {emitterIdentity(emitter), emitter.soundId,
         decision.normalizedDistance,
         decision.selectedForReflections});
  }
  std::ranges::stable_sort(
      implementation.diagnostics.rankedEmitters,
      {}, &AudioEmitterSimulationDiagnostics::normalizedRankingScore);

  std::vector<std::size_t> removals;
  for (std::size_t i = 0; i < implementation.activeEmitters.size(); ++i) {
    auto& active = implementation.activeEmitters[i];
    if (!active.present) {
      active.volume = advanceEmitterFade(
          active.volume, false, frameTime, VanishedEmitterFadeDuration);
      active.selectedForReflections = false;
      if (active.event) {
        requireFmod(
            active.event->setVolume(active.volume),
            "Unable to fade vanished emitter event");
      }
      if (active.volume <= 0.0f) removals.push_back(i);
      continue;
    }

    auto definition = std::ranges::find_if(
        implementation.emitterDefinitions,
        [&active](auto const& candidate) {
          return sameEmitterIdentity(active.definition, candidate) &&
                 active.definition.soundId == candidate.soundId;
        });
    auto const definitionIndex = static_cast<std::size_t>(
        definition - implementation.emitterDefinitions.begin());
    if (definition == implementation.emitterDefinitions.end() ||
        !decisions[definitionIndex].inRange) {
      removals.push_back(i);
      continue;
    }
    active.selectedForReflections =
        decisions[definitionIndex].selectedForReflections;
  }

  std::vector<std::size_t> additions;
  for (std::size_t i = 0; i < implementation.emitterDefinitions.size(); ++i) {
    if (decisions[i].inRange &&
        findActive(implementation.emitterDefinitions[i]) ==
            implementation.activeEmitters.end()) {
      additions.push_back(i);
    }
  }

  if (!removals.empty() || !additions.empty()) {
    std::unique_lock transition(implementation.sceneTransitionMutex);
    bool simulatorChanged = false;
    for (auto i : removals) {
      auto& active = implementation.activeEmitters[i];
      // Range exits are explicit stops; vanished sources have already reached
      // zero through the whole-event fade above.
      implementation.stopEvent(active, FMOD_STUDIO_STOP_IMMEDIATE);
      implementation.removeNativeSource(active);
      simulatorChanged = true;
    }
    for (auto i = removals.rbegin(); i != removals.rend(); ++i) {
      implementation.activeEmitters.erase(
          implementation.activeEmitters.begin() + *i);
    }

    try {
      for (auto definitionIndex : additions) {
        auto const& definition =
            implementation.emitterDefinitions[definitionIndex];
        IPLSourceSettings settings{};
        settings.flags = static_cast<IPLSimulationFlags>(
            IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
        auto native = std::make_shared<Implementation::NativeSource>();
        native->release = implementation.sourceRelease;
        if (implementation.sourceCreate(
                implementation.simulator, &settings, &native->source) !=
            IPL_STATUS_SUCCESS) {
          throw std::runtime_error("Unable to create Steam Audio source");
        }
        implementation.sourceAdd(
            native->source, implementation.simulator);
        simulatorChanged = true;

        Implementation::ActiveEmitter active;
        active.definition = definition;
        active.source = std::move(native);
        active.selectedForReflections =
            decisions[definitionIndex].selectedForReflections;
        active.fmodSourceHandle =
            implementation.fmodAddSource(active.source->source);
        if (active.fmodSourceHandle < 0) {
          implementation.sourceRemove(
              active.source->source, implementation.simulator);
          throw std::runtime_error(
              "Unable to register Steam Audio source with FMOD");
        }

        try {
          active.event =
              implementation.audioSystem->startEvent(definition.soundId);
          FMOD_3D_ATTRIBUTES attributes{};
          attributes.position = fmodVector(sourcePosition(definition));
          attributes.forward = {0.0f, 0.0f, -1.0f};
          attributes.up = {0.0f, 1.0f, 0.0f};
          requireFmod(
              active.event->set3DAttributes(&attributes),
              "Unable to position emitter event");
        } catch (...) {
          if (active.event) {
            implementation.stopEvent(
                active, FMOD_STUDIO_STOP_IMMEDIATE);
          }
          implementation.fmodRemoveSource(active.fmodSourceHandle);
          implementation.sourceRemove(
              active.source->source, implementation.simulator);
          throw;
        }
        implementation.activeEmitters.push_back(std::move(active));
      }
    } catch (...) {
      if (simulatorChanged) {
        implementation.simulatorCommit(implementation.simulator);
      }
      throw;
    }
    if (simulatorChanged) {
      implementation.simulatorCommit(implementation.simulator);
    }
  }

  for (auto& active : implementation.activeEmitters) {
    auto const coordinates = sourceCoordinates(sourcePosition(active.definition));
    auto directInputs = sourceSimulationInputs(
        coordinates, IPL_SIMULATIONFLAGS_DIRECT,
        implementation.options.features);
    implementation.sourceSetInputs(
        active.source->source, IPL_SIMULATIONFLAGS_DIRECT, &directInputs);

    if (active.event && active.present) {
      active.volume = advanceEmitterFade(
          active.volume, true, frameTime, VanishedEmitterFadeDuration);
      requireFmod(
          active.event->setVolume(active.volume),
          "Unable to fade in surviving emitter event");
      FMOD_3D_ATTRIBUTES attributes{};
      attributes.position = fmodVector(sourcePosition(active.definition));
      attributes.forward = {0.0f, 0.0f, -1.0f};
      attributes.up = {0.0f, 1.0f, 0.0f};
      requireFmod(
          active.event->set3DAttributes(&attributes),
          "Unable to update emitter event position");
    }

    active.reflectionContribution = advanceEmitterFade(
        active.reflectionContribution,
        active.present && active.selectedForReflections, frameTime,
        ReflectionFadeDuration);

    if (!active.spatializer && active.event) {
      FMOD::ChannelGroup* channelGroup{};
      if (active.event->getChannelGroup(&channelGroup) == FMOD_OK &&
          channelGroup) {
        int numDsps{};
        if (channelGroup->getNumDSPs(&numDsps) == FMOD_OK) {
          for (int i = 0; i < numDsps; ++i) {
            FMOD::DSP* dsp{};
            char name[64]{};
            if (channelGroup->getDSP(i, &dsp) == FMOD_OK && dsp &&
                dsp->getInfo(name, nullptr, nullptr, nullptr, nullptr) ==
                    FMOD_OK &&
                std::strcmp(name, SteamAudioSpatializerName) == 0) {
              active.spatializer = dsp;
              float authoredMix{};
              if (dsp->getParameterFloat(
                      ReflectionsMixLevelParameter, &authoredMix, nullptr,
                      0) == FMOD_OK) {
                active.authoredReflectionMix = std::max(authoredMix, 0.0f);
              }
              requireFmod(
                  dsp->setParameterInt(
                      SimulationOutputsHandleParameter,
                      active.fmodSourceHandle),
                  "Unable to bind emitter simulation outputs");
              break;
            }
          }
        }
      }
    }

    if (active.spatializer) {
      requireFmod(
          active.spatializer->setParameterBool(
              ApplyReflectionsParameter,
              implementation.options.features.reflections &&
                  (active.reflectionContribution > 0.0f ||
                   active.selectedForReflections)),
          "Unable to update emitter reflections");
      requireFmod(
          active.spatializer->setParameterFloat(
              ReflectionsMixLevelParameter,
              active.authoredReflectionMix *
                  active.reflectionContribution),
          "Unable to fade emitter reflections");
    }
  }

  implementation.publishSimulationState();
#else
  (void)listenerPosition;
  (void)frameTime;
#endif
}

void SteamAudio::runDirectSimulation() {
#if defined(WP_APPLICATION_USE_FMOD)
  auto& implementation = *mImplementation;
  auto listener = implementation.listener.load(std::memory_order_acquire);
  if (!listener ||
      !implementation.sceneReady.load(std::memory_order_acquire)) {
    return;
  }

  // Scene replacement is the only exclusive simulator operation. Never make
  // the game thread wait behind it: one skipped direct tick is preferable to
  // an update hitch at a World commit boundary.
  std::shared_lock simulation(
      implementation.sceneTransitionMutex, std::try_to_lock);
  if (!simulation.owns_lock()) return;

  IPLSimulationSharedInputs inputs{};
  inputs.listener = listener->coordinates;
  implementation.simulatorSetSharedInputs(
      implementation.simulator, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
  implementation.simulatorRunDirect(implementation.simulator);
#endif
}

void SteamAudio::setListener(mpp::Camera const& camera) {
#if defined(WP_APPLICATION_USE_FMOD)
  auto const& cameraPosition = camera.getPosition();
  auto const& cameraDirection = camera.getDirection();
  auto const& cameraUp = camera.getUp();
  auto finite = [](glm::vec3 const& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) &&
           std::isfinite(vector.z);
  };
  if (!finite(cameraPosition) || !finite(cameraDirection) ||
      !finite(cameraUp)) {
    throw std::runtime_error("FMOD listener camera basis is not finite");
  }

  // FMOD rejects a non-orthogonal listener basis. Camera vectors are normally
  // orthonormal, but rebuilding the basis here prevents accumulated floating
  // point error from turning a valid long-running session into an invalid
  // listener update.
  auto forwardLength = glm::length(cameraDirection);
  auto right = glm::cross(cameraDirection, cameraUp);
  auto rightLength = glm::length(right);
  if (forwardLength <= std::numeric_limits<float>::epsilon() ||
      rightLength <= std::numeric_limits<float>::epsilon()) {
    throw std::runtime_error("FMOD listener camera basis is degenerate");
  }
  auto forward = cameraDirection / forwardLength;
  right /= rightLength;
  auto up = glm::normalize(glm::cross(right, forward));

  auto position = fmodVector(cameraPosition);
  auto fmodForward = fmodVector(forward);
  auto fmodUp = fmodVector(up);
  FMOD_VECTOR const velocity{0.0f, 0.0f, 0.0f};
  requireFmod(mImplementation->coreSystem->set3DListenerAttributes(
                  0, &position, &velocity, &fmodForward, &fmodUp),
              "Unable to update FMOD listener attributes");

  auto listener = std::make_shared<Implementation::ListenerState const>(
      Implementation::ListenerState{
          {iplVector(right), iplVector(up), iplVector(forward),
           iplVector(cameraPosition)}});
  mImplementation->listener.store(std::move(listener),
                                  std::memory_order_release);
#else
  (void)camera;
#endif
}

}  // namespace bw::app
