#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Windows.h>

#include <fmod.hpp>
#include <phonon.h>

#include <willpower/application/AudioSystem.h>

#include "AcousticPresetResolver.h"
#include "AcousticScene.h"
#include "PeriodicSnapshotWorker.h"
#include "SteamAudio.h"

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

// #387's measured candidate is the temporary top preset until #401 makes
// presets data-driven. These are allocation maxima, not per-tick settings.
constexpr IPLint32 TopPresetMaxRays = 4096;
constexpr IPLint32 TopPresetMaxSources = 1;
constexpr IPLfloat32 TopPresetMaxDuration = 1.0f;
constexpr IPLint32 TopPresetMaxAmbisonicOrder = 1;
constexpr IPLint32 TopPresetBounces = 4;
constexpr IPLfloat32 TopPresetIrradianceMinDistance = 1.0f;
constexpr auto ReflectionTickInterval = std::chrono::milliseconds(100);

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

IPLVector3 iplVector(glm::vec3 const& value) {
  return {value.x, value.y, value.z};
}

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

  struct PublishedScene {
    AcousticScenePtr scene;
    uint64_t version{};
  };

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

  std::atomic<std::shared_ptr<ListenerState const>> listener;
  std::shared_mutex sceneTransitionMutex;
  std::atomic<bool> sceneReady{false};
  uint64_t simulationSceneVersion{};
  uint64_t nextSceneVersion{1};
  core::ArrangementWorldDataPtr publishedWorld;
  std::unique_ptr<PeriodicSnapshotWorker> reflectionWorker;

  ~Implementation() {
    // The worker and every scene it can retain must be gone before the
    // simulator, context, and dynamically loaded API are released.
    reflectionWorker.reset();
    if (simulator) simulatorRelease(&simulator);
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

  if (simulatorCreate(implementation.context, &simulationSettings,
                      &implementation.simulator) != IPL_STATUS_SUCCESS) {
    throw std::runtime_error("Unable to create Steam Audio simulator");
  }

  implementation.reflectionWorker =
      std::make_unique<PeriodicSnapshotWorker>(
          ReflectionTickInterval,
          [&implementation](PeriodicSnapshotWorker::SnapshotPtr const& value) {
            auto published =
                std::static_pointer_cast<Implementation::PublishedScene const>(
                    value);
            auto listener = implementation.listener.load(
                std::memory_order_acquire);
            if (!published || !published->scene || !listener ||
                !published->scene->mImplementation) {
              return;
            }

            if (published->version !=
                implementation.simulationSceneVersion) {
              std::unique_lock transition(
                  implementation.sceneTransitionMutex);
              implementation.simulatorSetScene(
                  implementation.simulator,
                  published->scene->mImplementation->scene);
              implementation.simulatorCommit(implementation.simulator);
              implementation.simulationSceneVersion = published->version;
              implementation.sceneReady.store(
                  true, std::memory_order_release);
            }

            IPLSimulationSharedInputs inputs{};
            inputs.listener = listener->coordinates;
            inputs.numRays = TopPresetMaxRays;
            inputs.numBounces = TopPresetBounces;
            inputs.duration = TopPresetMaxDuration;
            inputs.order = TopPresetMaxAmbisonicOrder;
            inputs.irradianceMinDistance = TopPresetIrradianceMinDistance;

            std::shared_lock simulation(
                implementation.sceneTransitionMutex);
            implementation.simulatorSetSharedInputs(
                implementation.simulator,
                IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
            implementation.simulatorRunReflections(
                implementation.simulator);
          });
#else
  (void)audioSystem;
#endif
}

SteamAudio::~SteamAudio() {
  delete mImplementation;
}

AcousticScenePtr SteamAudio::buildScene(
    core::ArrangementWorldDataPtr sourceWorld,
    AcousticPresetResolver const& resolver) const {
  if (!sourceWorld) {
    throw std::invalid_argument("Acoustic scene requires a World snapshot");
  }

  auto mesh = ExportAcousticSceneMesh(
      *sourceWorld, [&](std::string const& subMaterialId) -> AcousticPreset const& {
        return resolver.resolveSubMaterial(subMaterialId);
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
  auto published = std::make_shared<Implementation::PublishedScene const>(
      Implementation::PublishedScene{
          std::move(scene), mImplementation->nextSceneVersion++});
  mImplementation->reflectionWorker->publish(std::move(published));
#endif
  mImplementation->publishedWorld = std::move(sourceWorld);
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
  auto position = fmodVector(cameraPosition);
  auto forward = fmodVector(cameraDirection);
  auto up = fmodVector(cameraUp);
  FMOD_VECTOR const velocity{0.0f, 0.0f, 0.0f};
  if (mImplementation->coreSystem->set3DListenerAttributes(
          0, &position, &velocity, &forward, &up) != FMOD_OK) {
    throw std::runtime_error("Unable to update FMOD listener attributes");
  }

  auto listener = std::make_shared<Implementation::ListenerState const>(
      Implementation::ListenerState{
          {iplVector(glm::normalize(glm::cross(cameraDirection, cameraUp))),
           iplVector(cameraUp), iplVector(cameraDirection),
           iplVector(cameraPosition)}});
  mImplementation->listener.store(std::move(listener),
                                  std::memory_order_release);
#else
  (void)camera;
#endif
}

}  // namespace bw::app
