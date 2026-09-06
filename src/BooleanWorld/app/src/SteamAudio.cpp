#include <stdexcept>

#include <Windows.h>

#include <fmod.hpp>
#include <phonon.h>

#include <willpower/application/AudioSystem.h>

#include "SteamAudio.h"

namespace {

using InitializeFunction = void(IPLCALL*)(IPLContext);
using TerminateFunction = void(IPLCALL*)();
using SetHRTFFunction = void(IPLCALL*)(IPLHRTF);
using SetSimulationSettingsFunction = void(IPLCALL*)(IPLSimulationSettings);
using ContextReleaseFunction = void(IPLCALL*)(IPLContext*);
using HRTFReleaseFunction = void(IPLCALL*)(IPLHRTF*);

// #387's measured candidate is the temporary top preset until #401 makes
// presets data-driven. These are allocation maxima, not per-tick settings.
constexpr IPLint32 TopPresetMaxRays = 4096;
constexpr IPLint32 TopPresetMaxSources = 1;
constexpr IPLfloat32 TopPresetMaxDuration = 1.0f;
constexpr IPLint32 TopPresetMaxAmbisonicOrder = 1;

FARPROC steamAudioFunction(HMODULE module, char const* name) {
  auto function = GetProcAddress(module, name);
  if (!function) {
    throw std::runtime_error("Steam Audio FMOD plugin is missing a required API");
  }
  return function;
}

FMOD_VECTOR fmodVector(glm::vec3 const& value) {
  return {value.x, value.y, value.z};
}

}  // namespace

namespace bw::app {

struct SteamAudio::Implementation {
  FMOD::System* coreSystem{};
  IPLContext context{};
  IPLHRTF hrtf{};
  TerminateFunction terminate{};
  bool fmodInitialized{false};
  ContextReleaseFunction contextRelease{};
  HRTFReleaseFunction hrtfRelease{};

  ~Implementation() {
    if (fmodInitialized) terminate();
    if (hrtf) hrtfRelease(&hrtf);
    if (context) contextRelease(&context);
  }
};

SteamAudio::SteamAudio(wp::application::AudioSystem& audioSystem)
    : mImplementation(new Implementation) {
#if defined(WP_APPLICATION_USE_FMOD)
  auto& implementation = *mImplementation;
  implementation.coreSystem = audioSystem.getCoreSystem();
  if (!implementation.coreSystem) {
    throw std::runtime_error("FMOD core system is unavailable");
  }

  unsigned int pluginHandle{};
  if (implementation.coreSystem->loadPlugin("phonon_fmod.dll", &pluginHandle,
                                            0) != FMOD_OK) {
    throw std::runtime_error("Unable to load phonon_fmod.dll into FMOD core");
  }

  // loadPlugin owns the module. Its dependency phonon.dll is consequently
  // present before resolving either API's entry points.
  auto pluginModule = GetModuleHandleA("phonon_fmod.dll");
  if (!pluginModule) {
    throw std::runtime_error("FMOD loaded Steam Audio without its module");
  }
  auto phononModule = GetModuleHandleA("phonon.dll");
  if (!phononModule) {
    throw std::runtime_error("Steam Audio core module is unavailable");
  }

  auto contextCreate = reinterpret_cast<decltype(&iplContextCreate)>(
      steamAudioFunction(phononModule, "iplContextCreate"));
  auto hrtfCreate = reinterpret_cast<decltype(&iplHRTFCreate)>(
      steamAudioFunction(phononModule, "iplHRTFCreate"));
  implementation.contextRelease = reinterpret_cast<ContextReleaseFunction>(
      steamAudioFunction(phononModule, "iplContextRelease"));
  implementation.hrtfRelease = reinterpret_cast<HRTFReleaseFunction>(
      steamAudioFunction(phononModule, "iplHRTFRelease"));
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
  simulationSettings.flags = IPL_SIMULATIONFLAGS_DIRECT |
                             IPL_SIMULATIONFLAGS_REFLECTIONS;
  simulationSettings.sceneType = IPL_SCENETYPE_DEFAULT;
  simulationSettings.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
  simulationSettings.maxNumOcclusionSamples = 1;
  simulationSettings.maxNumRays = TopPresetMaxRays;
  simulationSettings.numDiffuseSamples = 1;
  simulationSettings.maxDuration = TopPresetMaxDuration;
  simulationSettings.maxOrder = TopPresetMaxAmbisonicOrder;
  simulationSettings.maxNumSources = TopPresetMaxSources;
  simulationSettings.numThreads = 1;
  simulationSettings.rayBatchSize = 1;
  simulationSettings.samplingRate = sampleRate;
  simulationSettings.frameSize = static_cast<IPLint32>(frameSize);
  setSimulationSettings(simulationSettings);
#else
  (void)audioSystem;
#endif
}

SteamAudio::~SteamAudio() {
  delete mImplementation;
}

void SteamAudio::setListener(mpp::Camera const& camera) {
#if defined(WP_APPLICATION_USE_FMOD)
  auto position = fmodVector(camera.getPosition());
  auto forward = fmodVector(camera.getDirection());
  auto up = fmodVector(camera.getUp());
  FMOD_VECTOR const velocity{0.0f, 0.0f, 0.0f};
  if (mImplementation->coreSystem->set3DListenerAttributes(
          0, &position, &velocity, &forward, &up) != FMOD_OK) {
    throw std::runtime_error("Unable to update FMOD listener attributes");
  }
#else
  (void)camera;
#endif
}

}  // namespace bw::app
