#define NOMINMAX

#include <algorithm>
#include <bit>
#include <cassert>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <GL/glew.h>

#include <mpp/BoxModelStream.h>
#include <mpp/ModelRenderParams.h>
#include <mpp/ResourceManager.h>
#include <mpp/RenderGraphExecutor.h>
#include <mpp/helper/FreeCamera.h>

#include <utils/Image.h>

#include <willpower/application/StateExceptions.h>

#include <willpower/collide/ColliderCircle.h>
#include <willpower/collide/ColliderAABB.h>

#include <willpower/geometry/MeshQuery.h>

#include <willpower/viz/DynamicLineRenderer.h>
#include <willpower/viz/CollisionSimulationRenderer.h>

#include <applib/ModelInstance.h>
#include <applib/VisualSpriteEntityFacade.h>

#include <core/Utils.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/LiquidType.h>

#include <common/BoundedDeque.h>
#include <common/GameDefines.h>

#include "imgui/imgui.h"
#include "imgui/implot.h"

#include "StatePlayBooleanWorld.h"

#include "PlayerLiquidTraversal.h"
#include "PlayerTorchShadows.h"
#include "BooleanWorldModel.h"
#include "EntityHandlerBooleanWorld.h"
#include "EntityType.h"
#include "TriMeshDataProvider.h"
#include "TriMeshEntityFacadeFactory.h"
#include "Map.h"
#include "PlayerLocation.h"
#include "PlayerView.h"
#include "ReactiveCamera.h"
#include "GameException.h"
#include "ImGuiDllBoundaryState.h"
#include "LiquidReflectionSelection.h"

#define CLIPPING_RECORD_COUNT_MAX 10
#define DISPLAY_MESSAGE_COUNT_MAX 128
#define DISPLAY_MESSAGE_TIME 10

using namespace std;
using namespace wp;

DisplayMessage::Level gDisplayMessageLevel = DisplayMessage::Level::Debug;

// Bounds of the debug slider for mouse sensitivity. Configuration is not held
// to them: they only need to cover the range worth dragging through.
const float gImGui_MouseSensitivityMin = 0.03f;
const float gImGui_MouseSensitivityMax = 3.0f;

constexpr int32_t gNoMaterialOverride = -1;
constexpr float gAuthoredMaterialScale = 1.0f;
constexpr float gPlayerTorchMarkerSize = 2.0f;
constexpr char gPlayerTorchMarkerModelName[] =
    "BooleanWorld.PlayerTorchMarker.Model";
const SecondaryMaterialOptions gNoSecondaryMaterial{};

// The graph image backing the world shaders' LIQUID_RETENTION output. MPP
// registers every RenderPipelineOptions::sceneExtraOutputs entry under a
// "SceneExtra." prefix, so this is the name the ambient-occlusion composite
// has to be pointed at - not the bare output name.
constexpr char gLiquidRetentionImageName[] = "SceneExtra.LIQUID_RETENTION";

static_assert(
    bw::app::maximumPlanarLiquidSurfaces ==
    mpp::WaterReflectionOptions::MaxPlanarPlanes);

std::vector<mpp::PlanarReflectionPlaneDescriptor>
discoverLiquidReflectionPlanes(
    bw::core::WorldData const& snapshot, mpp::Camera& camera,
    bw::app::LiquidReflectionSelectionPolicy& selectionPolicy) {
  auto const& arrangement = snapshot.getArrangement();
  auto const& liquidDepths = snapshot.getLiquidDepths();
  std::vector<bw::app::LiquidSurfaceTriangle> surfaces;
  surfaces.reserve(snapshot.getTriangles().size());
  for (auto const& triangle : snapshot.getTriangles()) {
    auto depth = liquidDepths[triangle.face];
    if (depth <= 0.0f) continue;
    auto const& properties =
        arrangement.palette[arrangement.faces[triangle.face].paletteIndex];
    auto elevation = properties.floorZ + depth;
    bw::app::LiquidSurfaceTriangle surface;
    surface.elevation = elevation;
    for (int index = 0; index < 3; ++index) {
      auto const& vertex = arrangement.vertices[triangle.v[index]];
      surface.vertices[index] = {
          bw::core::arr::ToWorldCoordinate(vertex.x), elevation,
          -bw::core::arr::ToWorldCoordinate(vertex.y)};
    }
    surfaces.push_back(surface);
  }

  auto selected = selectionPolicy.select(
      surfaces,
      camera.getProjectionTransform() * camera.getViewTransform(),
      camera.getPosition());
  std::vector<mpp::PlanarReflectionPlaneDescriptor> planes;
  planes.reserve(selected.size());
  for (auto const& surface : selected) {
    planes.push_back({surface.elevation,
                      surface.viewerAbove ? mpp::ReflectionPlaneSide::Above
                                          : mpp::ReflectionPlaneSide::Below,
                      surface.minimumElevation, surface.maximumElevation});
  }
  return planes;
}

// The extra scene-pass outputs the world pipeline declares when ambient
// occlusion is on.
//
// Every scene program declares @Out(COLOUR), @Out(BLOOM_MASK),
// @Out(SHADING_NORMAL) and @Out(LIQUID_RETENTION) in that order, and MPP
// compiles fragment outputs to fixed locations from declaration order - it has
// no mechanism to drop one for a pipeline variant that does not want it. The
// graph only reserves built-in attachments for locations 1 and 2 when GTAO
// sources its normals from that MRT slot, and appends these extras after
// whichever built-ins are in play. So outside that one mode the extras begin
// at location 1, and declaring LIQUID_RETENTION alone would bind it there and
// capture BLOOM_MASK's constant 0 - ambient occlusion at full strength
// everywhere, submerged or not. Naming the intervening outputs keeps
// LIQUID_RETENTION on location 3 in every mode; nothing samples their
// contents, hence the cheapest format.
//
// This is also the count that shifts every later graph image along, so
// renderWorldThroughTarget resolves its output index through this same
// function rather than hard-coding a second copy of the total.
std::vector<mpp::RenderPipelineSceneExtraOutput> worldSceneExtraOutputs(
    bool reservesMrtNormalSlots) {
  std::vector<mpp::RenderPipelineSceneExtraOutput> outputs;
  if (!reservesMrtNormalSlots) {
    outputs.push_back({"BLOOM_MASK", mpp::GraphImageFormat::R8});
    outputs.push_back({"SHADING_NORMAL", mpp::GraphImageFormat::R8});
  }
  outputs.push_back({"LIQUID_RETENTION", mpp::GraphImageFormat::R8});
  return outputs;
}

// ImGui colours go here so they don't clutter up the header file
const ImColor gImGui_MapBackgroundColour{0.2f, 0.2f, 0.8f};
const ImColor gImGui_TriangulationLineColour{0.8f, 0.8f, 0.2f};
const ImColor gImGui_MapBorderColour{1.0f, 1.0f, 0.7f};
const ImColor gImGui_CollisionLineSolidColour{0.8f, 0.8f, 0.2f};
// Lighter than the solid lines: two-sided walls are there to be seen beside
// the ones that stop the player, not mistaken for them.
const ImColor gImGui_CollisionLine2WayColour{1.0f, 1.0f, 0.65f};
const ImColor gImGui_ViewAreaColour{0.5f, 0.5f, 0.5f};

StatePlayBooleanWorld::StatePlayBooleanWorld()
    : StatePlay(), mGlobalTime(0.0), mWorldCollisionSim(nullptr), mPlayerCollider(nullptr), mAllLayers(false), mPlayerPolygonIndex(-1), mPlayerBorderIntersectIndex(-1), mCollisionsProcessed(0), mwRenderer(nullptr), mPlayerPrevAngle(0), mPlayerPrevPitch(0), mExitScheduled(false) {
}

StatePlayBooleanWorld::~StatePlayBooleanWorld() {
  delete mWorldCollisionSim;
}

bw::core::LayerSelection StatePlayBooleanWorld::layerSelection() const {
  return mAllLayers ? bw::core::SelectAllLayers() : bw::core::SelectLayer(0);
}

Map* StatePlayBooleanWorld::getMap() {
  return static_cast<Map*>(mMap.get());
}

Map const* StatePlayBooleanWorld::getMap() const {
  return static_cast<Map const*>(mMap.get());
}

applib::PhysicalStats& StatePlayBooleanWorld::getPlayerPhysicalStats() {
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  return model->entityHandler->getEntityComponent<applib::PhysicalStats>(mEntityMgr->getPlayerEntity());
}

applib::PhysicalStats const& StatePlayBooleanWorld::getPlayerPhysicalStats() const {
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  return model->entityHandler->getEntityComponent<applib::PhysicalStats>(mEntityMgr->getPlayerEntity());
}

void StatePlayBooleanWorld::createCamera() {
  float aspectRatio = mwRenderSystem->getWindowWidth() / (float)mwRenderSystem->getWindowHeight();

  auto const& physicalStats = getPlayerPhysicalStats();
  auto camera = new ReactiveCamera(
      glm::vec3(
          physicalStats.position.x, BW_PLAYER_HEIGHT,
          -physicalStats.position.y),
      bw::app::cameraYaw(physicalStats.angle), physicalStats.pitch, BW_PLAYER_FOV, aspectRatio);
  camera->setClipDistances(0.1f, BW_PLAYER_VIEW_DISTANCE + 10);

  mCamera3d = shared_ptr<mpp::Camera>(camera);
}

mpp::RenderPipelinePtr const& StatePlayBooleanWorld::getOrCreateWorldRenderPipeline(
    bw::app::RenderScale renderScale,
    bw::app::AntiAliasing antiAliasing,
    std::vector<mpp::PlanarReflectionPlaneDescriptor> const& planarPlanes) {
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  auto technique = model->getWaterReflectionTechnique();
  auto planarResolution = model->getPlanarReflectionResolution();
  auto key = std::string(bw::app::renderScaleName(renderScale)) + ".aa-" +
             std::string(bw::app::antiAliasingName(antiAliasing)) +
             (mDebugDisplay.depthPrepass ? ".depth-prepass"
                                         : ".no-depth-prepass") +
             "." +
             std::string(bw::app::waterReflectionTechniqueName(technique)) +
             "." + std::string(
                 bw::app::planarReflectionResolutionName(planarResolution));
  if (technique == bw::app::WaterReflectionTechnique::Planar) {
    key += mPlanarReflectionSessionFailed
               ? ".reflection-failed"
               : ".planes-" + std::to_string(planarPlanes.size());
    for (auto const& plane :
         mPlanarReflectionSessionFailed
             ? std::vector<mpp::PlanarReflectionPlaneDescriptor>{}
             : planarPlanes) {
      key += "." +
             std::to_string(std::bit_cast<std::uint32_t>(plane.elevation)) +
             (plane.viewerSide == mpp::ReflectionPlaneSide::Above
                  ? ".above"
                  : ".below") +
             "." + std::to_string(std::bit_cast<std::uint32_t>(
                       plane.minimumMatchingElevation)) +
             "." + std::to_string(std::bit_cast<std::uint32_t>(
                       plane.maximumMatchingElevation));
    }
  }
  auto& pipeline = mWorldRenderPipelines[key];
  if (pipeline) return pipeline;

  auto const& target = mwRenderer->getRenderTarget(renderScale);
  auto pipelineName = getName() + ".World." + key;

  mpp::AntiAliasingSamples msaa = mpp::AntiAliasingSamples::Off;
  switch (bw::app::antiAliasingMsaaSamples(antiAliasing)) {
    case 2:
      msaa = mpp::AntiAliasingSamples::X2;
      break;
    case 4:
      msaa = mpp::AntiAliasingSamples::X4;
      break;
    case 8:
      msaa = mpp::AntiAliasingSamples::X8;
      break;
  }

  // MPP applies the selected AA stage through immutable render-pipeline output
  // options. Keep every option's named output offscreen: Presentation is an
  // external image backed by the screen, not the texture composited below.
  auto ambientOcclusionMethod = mpp::AmbientOcclusionMethod::None;
  if (mDebugDisplay.ambientOcclusionEnabled) {
    switch (mDebugDisplay.ambientOcclusion) {
      case bw::app::AmbientOcclusion::Ssao:
        ambientOcclusionMethod = mpp::AmbientOcclusionMethod::Ssao;
        break;
      case bw::app::AmbientOcclusion::GtaoDepth:
      case bw::app::AmbientOcclusion::GtaoNormals:
        ambientOcclusionMethod = mpp::AmbientOcclusionMethod::Gtao;
        break;
      case bw::app::AmbientOcclusion::None:
        break;
    }
  }
  auto ambientOcclusionEnabled =
      ambientOcclusionMethod != mpp::AmbientOcclusionMethod::None;

  mpp::RenderPipelineOptions options;
  options.mode = mpp::RenderPipelineMode::GraphLegacyForward;
  mpp::RenderPipelineOutput output;
  output.name = "World";
  // A dry Planar view has no Water or Planar pass by contract, so its named
  // output is the final opaque/AO image. Every branch with a reflection source
  // uses the distinct post-water image.
  auto planarWithoutVisibleLiquid =
      technique == bw::app::WaterReflectionTechnique::Planar &&
      planarPlanes.empty();
  output.image = planarWithoutVisibleLiquid
                     ? (ambientOcclusionEnabled
                            ? "AmbientOcclusionComposite"
                            : "SceneLdr")
                     : "WaterComposite";
  output.antiAliasing.msaa = msaa;
  output.antiAliasing.fxaa = bw::app::antiAliasingIsFxaa(antiAliasing);
  options.outputs.push_back(output);
  options.generatedWater = true;
  options.waterReflections.enabled = !mPlanarReflectionSessionFailed;
  options.waterReflections.technique =
      technique == bw::app::WaterReflectionTechnique::Planar
          ? mpp::WaterReflectionTechnique::Planar
          : mpp::WaterReflectionTechnique::ScreenSpace;
  switch (planarResolution) {
    case bw::app::PlanarReflectionResolution::Full:
      options.waterReflections.planarResolution =
          mpp::PlanarReflectionResolution::Full;
      break;
    case bw::app::PlanarReflectionResolution::Quarter:
      options.waterReflections.planarResolution =
          mpp::PlanarReflectionResolution::Quarter;
      break;
    case bw::app::PlanarReflectionResolution::Half:
      options.waterReflections.planarResolution =
          mpp::PlanarReflectionResolution::Half;
      break;
  }
  options.waterReflections.planarPlanes = planarPlanes;
  options.depthPrepass = mDebugDisplay.depthPrepass;
  options.ambientOcclusion.method = ambientOcclusionMethod;
  options.ambientOcclusion.ssao = mDebugDisplay.ssao;
  options.ambientOcclusion.gtao = mDebugDisplay.gtao;
  if (ambientOcclusionEnabled) {
    // Fade AO darkening out on submerged geometry as whatever's covering it
    // gets deeper/more opaque, rather than applying the same geometric AO
    // regardless of what's absorbing the light on the way to the eye. See
    // worldSceneExtraOutputs for why the other outputs are named here too.
    auto reservesMrtNormalSlots =
        ambientOcclusionMethod == mpp::AmbientOcclusionMethod::Gtao &&
        mDebugDisplay.gtao.normalSource == mpp::GTAONormalSource::Mrt;
    options.sceneExtraOutputs = worldSceneExtraOutputs(reservesMrtNormalSlots);
    options.ambientOcclusion.modulationInput = gLiquidRetentionImageName;
  }
  // Scale and AA variants all participate in this one render-system domain;
  // switching pipelines never allocates or renders a second cubemap.
  bw::app::joinPlayerTorchShadowDomain(options);

  pipeline = mwRenderSystem->getOrCreateRenderPipeline(pipelineName, options);
  pipeline->resize(target->getWidth(), target->getHeight());
  return pipeline;
}

mpp::RenderPipelinePtr const&
StatePlayBooleanWorld::getOrCreateFragmentOverdrawPipeline(
    bw::app::RenderScale renderScale) {
  auto const& target = mwRenderer->getRenderTarget(renderScale);
  auto depthPrepassIndex = mDebugDisplay.depthPrepass ? std::size_t{1}
                                                     : std::size_t{0};
  auto& pipeline = mFragmentOverdrawPipelines[depthPrepassIndex];
  if (!pipeline) {
    mpp::RenderPipelineOptions options;
    options.mode = mpp::RenderPipelineMode::GraphLegacyForward;
    mpp::RenderPipelineOutput output;
    output.name = "FragmentOverdraw";
    output.image = "SceneLdr";
    options.outputs.push_back(output);
    options.depthPrepass = mDebugDisplay.depthPrepass;
    // Deliberately omit ambient occlusion, anti-aliasing, and the Player Torch
    // shadow domain. Apart from the optional depth prepass, this graph consists
    // only of the scene render and resolve.
    pipeline = mwRenderSystem->getOrCreateRenderPipeline(
        getName() + ".World.FragmentOverdraw." +
            (mDebugDisplay.depthPrepass ? "depth-prepass"
                                        : "no-depth-prepass"),
        options);
  }
  pipeline->resize(target->getWidth(), target->getHeight());
  return pipeline;
}

void StatePlayBooleanWorld::setupMapRenderer(applib::StateTransitionData* transitionData) {
  mLiquidReflectionSelection.reset();
  mLiquidReflectionSelectionTechnique.reset();
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  mDebugDisplay.ambientOcclusion = model->getAmbientOcclusion();
  mDebugDisplay.ambientOcclusionEnabled =
      mDebugDisplay.ambientOcclusion != bw::app::AmbientOcclusion::None;
  mDebugDisplay.gtao.normalSource =
      mDebugDisplay.ambientOcclusion == bw::app::AmbientOcclusion::GtaoNormals
          ? mpp::GTAONormalSource::Mrt
          : mpp::GTAONormalSource::Depth;

  mwRenderer = static_cast<WorldRenderer*>(transitionData->userData);
  auto world = getMap()->getWorld();
  mwRenderer->create(mScene, world, mwRenderSystem, mwRenderResourceMgr);
  mDebugDisplay.wedgeQuality =
      int(world->getWedgeGenerationParameters().quality);
  createPlayerTorchMarker();

  // Configure before constructing any participating pipeline. The first frame
  // will update this from the same computed light position used by materials.
  // Keep a separate diagnostic copy: F5 never changes the launch configuration.
  mDebugDisplay.playerTorch = model->getPlayerTorchOptions();
  mDebugDisplay.playerTorchShadows = bw::app::playerTorchShadowSessionOptions(
      model->getShadowOptions());
  mPlayerTorchShadowHardwareFallback = false;
  mPlayerTorchShadowRequestedEnabled = model->getShadowOptions().enabled;
  auto const& physicalStats = getPlayerPhysicalStats();
  auto initialPosition = glm::vec3{
      physicalStats.position.x,
      physicalStats.floorZ + BW_PLAYER_EYE_HEIGHT,
      -physicalStats.position.y};
  mwRenderSystem->configureShadowDomain(
      std::string(bw::app::playerTorchShadowDomain),
      bw::app::playerTorchMppShadowOptions(
          model->getShadowOptions(), initialPosition));

  auto vignetteResource =
      mwResourceMgr->getResource("VignetteProgram", "World");
  assert(vignetteResource);
  mVignetteProgram = vignetteResource->getMppResource();
  assert(mVignetteProgram);

  auto fragmentOverdrawResolveResource =
      mwResourceMgr->getResource("FragmentOverdrawResolveProgram", "World");
  assert(fragmentOverdrawResolveResource);
  mFragmentOverdrawResolveProgram =
      fragmentOverdrawResolveResource->getMppResource();
  assert(mFragmentOverdrawResolveProgram);

  // Keep the default path ready. Other supported sample counts are allocated
  // only if selected in the debug GUI.
  for (auto renderScale : bw::app::allRenderScales) {
    getOrCreateWorldRenderPipeline(
        renderScale, bw::app::AntiAliasing::Off, {});
  }
}

void StatePlayBooleanWorld::createPlayerTorchMarker() {
  auto materialResource =
      mwResourceMgr->getResource("Material.PlayerTorchMarker", "World");
  assert(materialResource);
  auto material = materialResource->getMppResource();
  assert(material);

  mpp::mesh::MeshSpecification meshSpec{
      mpp::mesh::Primitive::Type::Triangles,
      mpp::mesh::VertexBufferStorageType::Static};
  meshSpec.setIndexedVertices(true);
  auto layout = meshSpec.createVertexBufferAttributeLayout(true);
  layout->createAttribute(
      mpp::mesh::Vertex::Component::Position3,
      mpp::mesh::Vertex::DataType::Float, false);

  auto modelStream = make_shared<mpp::BoxModelStream>(
      mwRenderResourceMgr, meshSpec, material->getName(),
      gPlayerTorchMarkerSize, gPlayerTorchMarkerSize,
      gPlayerTorchMarkerSize);
  mPlayerTorchMarkerModel = mwRenderResourceMgr
                                ->declareResource(
                                    gPlayerTorchMarkerModelName, modelStream)
                                .first;
  mPlayerTorchMarker = mScene->add3dModel(mPlayerTorchMarkerModel);
  // The marker identifies the light but must not occlude that light.
  mPlayerTorchMarker->getParams()->setModelFlags(0);
  mPlayerTorchMarkerPositionValid = false;
}

void StatePlayBooleanWorld::destroyPlayerTorchMarker() {
  if (mPlayerTorchMarker) {
    mScene->remove3dModel(mPlayerTorchMarker);
    mPlayerTorchMarker.reset();
  }
  // Render submissions can retain the SceneModel3d until their pipeline is
  // retired later in teardown. Leave the now-unreferenced model resource to
  // ResourceManager rather than deleting it while that submission is alive.
  mPlayerTorchMarkerModel.reset();
  mPlayerTorchMarkerPositionValid = false;
}

void StatePlayBooleanWorld::updatePlayerTorchMarker(
    glm::vec3 const& lightPosition) {
  if (!mPlayerTorchMarker) {
    return;
  }

  if (!mPlayerTorchMarkerPositionValid ||
      mPlayerTorchMarkerPosition.x != lightPosition.x ||
      mPlayerTorchMarkerPosition.y != lightPosition.y ||
      mPlayerTorchMarkerPosition.z != lightPosition.z) {
    mPlayerTorchMarker->resetTransform();
    mPlayerTorchMarker->translate(lightPosition);
    mPlayerTorchMarkerPosition = lightPosition;
    mPlayerTorchMarkerPositionValid = true;
  }

  auto flags = mDebugDisplay.lightDistance > 0.0f &&
                       !mDebugDisplay.fragmentOverdraw
                   ? mpp::ModelRenderParams::Flag_Visible
                   : 0;
  mPlayerTorchMarker->getParams()->setModelFlags(flags);
}

map<string, tuple<wp::viz::Renderer*, int, bool>> StatePlayBooleanWorld::createAdditionalRenderers(mpp::ResourceManager* renderResourceMgr) {
  VAR_UNUSED(renderResourceMgr);

  return {};
}

void StatePlayBooleanWorld::registerInput() {
  using namespace application;

  //											Keys pressed/released/down		// Buttons P/R/D	Wheel U/D,		modifiers	gui-disabled
  registerInputState("Exit", {Key::Escape}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("Up", {}, {}, {Key::UpArrow}, {}, {}, {}, false, false, 0, true);
  registerInputState("Down", {}, {}, {Key::DownArrow}, {}, {}, {}, false, false, 0, true);
  registerInputState("Left", {}, {}, {Key::LeftArrow}, {}, {}, {}, false, false, 0, true);
  registerInputState("Right", {}, {}, {Key::RightArrow}, {}, {}, {}, false, false, 0, true);
  registerInputState("GenClip", {Key::P}, {}, {}, {}, {}, {}, false, false, 0, true);
  registerInputState("Debug.Minimap", {Key::F2}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("Debug.CollisionSim", {Key::F3}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("Debug.ClipGen", {Key::F4}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("Debug.Options", {Key::F5}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("ToggleAllLayers", {Key::F9}, {}, {}, {}, {}, {}, false, false, 0, true);
  registerInputState("RenderGraphCapture", {Key::F10}, {}, {}, {}, {}, {}, false, false, 0, false);
  registerInputState("Screenshot", {Key::F11}, {}, {}, {}, {}, {}, false, false, 0, false);
}

void StatePlayBooleanWorld::setupPlayerCollision() {
  mWorldCollisionSim = new WorldCollisionSim(this);

  auto const& physicalStats = getPlayerPhysicalStats();
  auto playerCollider = make_unique<wp::collide::ColliderCircle>(
      physicalStats.position, BW_PLAYER_RADIUS);
  auto playerColliderObserver = playerCollider.get();

  mWorldCollisionSim->addSlidingCollider(
      move(playerCollider),
      [this] { ++mCollisionsProcessed; });
  mPlayerCollider = playerColliderObserver;

  applib::ModelInstance::entityHandler()->setupCollisions(mWorldCollisionSim, mPlayerCollider);
}

bool StatePlayBooleanWorld::playerInWorld() const {
  return mPlayerPolygonIndex >= 0;
}

bool StatePlayBooleanWorld::playerIntersectsWorldBorders() const {
  return mPlayerBorderIntersectIndex >= 0;
}

void StatePlayBooleanWorld::getWorldInput(wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* verticalEffort, float frameTime) const {
  auto const& player = mEntityMgr->getPlayerEntity();
  auto entityHandler = static_cast<EntityHandlerBooleanWorld*>(applib::ModelInstance::get()->entityHandler.get());

  wp::Vector2 velocity;
  float curPitch, newPitch;
  entityHandler->peekInput(player, curPosition, newPosition, curAngle, newAngle, &curPitch, &newPitch, &velocity, verticalEffort, frameTime);
}

void StatePlayBooleanWorld::createWorldCollisions(
    wp::Vector2 const& predictedPosition) {
  mWorldCollisionSim->clearLines();
  if (!mWorldData) {
    return;
  }

  auto const& arrangement = mWorldData->getArrangement();
  auto const& walls = mWorldData->getWalls();
  auto radius = BW_PLAYER_SPEED + BW_PLAYER_RADIUS;
  auto const& playerPosition = getPlayerPhysicalStats().position;
  auto swimming = isPlayerSwimming();
  auto descendingForTraversal =
      bw::app::isDescendingForTallStepTraversal(
          swimming, mPlayerVerticalVelocity);
  for (auto wallIndex : mWorldData->getWallsNearForTraversal(
           predictedPosition, radius, playerPosition,
           descendingForTraversal)) {
    auto const& wall = walls[wallIndex];
    auto const& edge = arrangement.edges[wall.edge];
    auto const& fixed0 = arrangement.vertices[edge.v[0]];
    auto const& fixed1 = arrangement.vertices[edge.v[1]];
    wp::Vector2 v0{
        bw::core::arr::ToWorldCoordinate(fixed0.x),
        bw::core::arr::ToWorldCoordinate(fixed0.y)};
    wp::Vector2 v1{
        bw::core::arr::ToWorldCoordinate(fixed1.x),
        bw::core::arr::ToWorldCoordinate(fixed1.y)};

    // A tall FloorStep is withheld while the player crosses over it - falling
    // off the ledge, or swimming above the pool floor it encloses - so by the
    // time the traversal rules admit it again the player may already be
    // standing inside it. Reinstating it there does not block an approach; it
    // wedges the collider against a wall it is behind, and the sliding
    // response then strips the inward component of every direction at once,
    // freezing the player in place. Only walls blocking purely by the
    // step-height rule are skipped this way - authored collision, Borders and
    // clearance limits stay solid regardless. Never suppress this wall for a
    // swimmer: the deliberate climb-out action moves their whole collider
    // clear of the edge after checking view pitch, facing and reach. Letting
    // horizontal collision cross here would bypass those checks.
    auto blocksOnlyByStepHeight =
        wall.kind == bw::core::arr::ArrangementWallKind::FloorStep &&
        !edge.collidesOverride.value_or(false) &&
        wall.clearance >= BW_PLAYER_HEIGHT;
    if (blocksOnlyByStepHeight &&
        bw::app::maySuppressOverlappingTallStep(swimming) &&
        playerPosition.distanceToLine(v0, v1) < BW_PLAYER_RADIUS) {
      continue;
    }

    mWorldCollisionSim->addLine(v0, v1, wallIndex);
  }
}

void StatePlayBooleanWorld::createGameObjects(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) {
  VAR_UNUSED(resourceMgr);
  VAR_UNUSED(renderSystem);
  VAR_UNUSED(renderResourceMgr);
  VAR_UNUSED(args);

  setupPlayerCollision();
}

void StatePlayBooleanWorld::destroyGameObjects() {
  mLiquidReflectionSelection.reset();
  mLiquidReflectionSelectionTechnique.reset();
  if (auto dataGenerator = getWDG()) {
    dataGenerator->stopGenerationSchedule();
    dataGenerator->unregisterGenerationCallback(mGenerationCallbackToken);
  }
  mGenerationCallbackToken =
      bw::core::DynamicWorldDataGenerator::InvalidGenerationCallbackToken;

  applib::ModelInstance::entityHandler()->setupCollisions(nullptr, nullptr);
  delete mWorldCollisionSim;
  mWorldCollisionSim = nullptr;
  mPlayerCollider = nullptr;

  destroyPlayerTorchMarker();

  // RenderSystem keeps its own reference to each named pipeline (see
  // getOrCreateWorldRenderPipeline), so resetting mWorldRenderPipelines
  // alone would not destroy them. Evict them here, while BooleanWorld.dll
  // is still loaded, instead of leaving that to RenderSystem's own
  // teardown - which runs after this DLL has already been unloaded.
  for (auto& [key, pipeline] : mWorldRenderPipelines) {
    if (pipeline) {
      mwRenderSystem->removeRenderPipeline(pipeline->getName());
      pipeline.reset();
    }
  }
  mWorldRenderPipelines.clear();
  for (auto& pipeline : mFragmentOverdrawPipelines) {
    if (pipeline) {
      mwRenderSystem->removeRenderPipeline(pipeline->getName());
      pipeline.reset();
    }
  }
  mFragmentOverdrawResolveProgram.reset();
  mVignetteProgram.reset();
}

void StatePlayBooleanWorld::setupEntityFacades() {
  applib::VisualSpriteEntityFacadeRenderOptions options;

  options.rotationType = wp::viz::RotationOptions::None;

  // Create Quad EntityFacade
  auto quadsResource = mwResourceMgr->getResource("EntityAnimations");
  auto animDatabase = applib::ModelInstance::animationDatabase();
  mEntityMgr->registerFacadeFactory("Sprites", new applib::VisualSpriteEntityFacadeFactory(quadsResource, animDatabase));

  createEntityFacade(
      "Sprites",
      {(int)EntityType::Player},
      options,
      64);

  // Create triangles EntityFacade
  auto trisProviderFactory = [](auto facade) {
    return make_shared<TriMeshDataProvider>(facade);
  };

  mEntityMgr->registerFacadeFactory("TriMesh", new TriMeshEntityFacadeFactory(trisProviderFactory, nullptr));
}

vector<string> StatePlayBooleanWorld::getDebuggingText() const {
  auto mouseScreen = getMouseScreenPosition();
  auto mouseWorld = getMouseWorldPosition();

  auto const& physicalStats = getPlayerPhysicalStats();
  auto playerPrimIndex = getPlayerPrimitive();
  auto floorHeight = getPlayerFloorHeight();
  auto ceilingHeight = getPlayerCeilingHeight();
  auto liquidSubmersionDepth = getPlayerLiquidSubmersionDepth();

  vector<string> lines{
      STR_FORMAT("Mouse screen: {:.0f},{:.0f}", mouseScreen.x, mouseScreen.y),
      STR_FORMAT("Mouse world: {:.2f},{:.2f}", mouseWorld.x, mouseWorld.y),
      STR_FORMAT("Player world: {:.2f},{:.2f}", physicalStats.position.x, physicalStats.position.y),
      STR_FORMAT("Player floor/ceil: {:.2f},{:.2f}", floorHeight, ceilingHeight),
      STR_FORMAT("Player height/vZ: {:.2f},{:.2f}", physicalStats.floorZ, mPlayerVerticalVelocity),
      STR_FORMAT("Player angle: {:.2f}", physicalStats.angle),
      STR_FORMAT("Player poly: {}", mPlayerPolygonIndex),
      STR_FORMAT("Player prim: {}", playerPrimIndex),
      STR_FORMAT("Collision count: {}", mCollisionsProcessed)

  };

  if (liquidSubmersionDepth > 0.0f) {
    auto liquidType = mWorldData->getLiquidType(physicalStats.position);
    lines.push_back(STR_FORMAT(
        "Player submerged: {:.2f} ({})", liquidSubmersionDepth,
        bw::core::LiquidTypeName(liquidType)));
  }

  return lines;
}

uint32_t StatePlayBooleanWorld::getPrimitiveAtPosition(wp::Vector2 const& pos) const {
  return mWorldData ? mWorldData->getContainingPrimitiveIndex(pos) : ~0u;
}

uint32_t StatePlayBooleanWorld::getPlayerPrimitive() const {
  auto const& physicalStats = getPlayerPhysicalStats();

  return getPrimitiveAtPosition(physicalStats.position);
}

wp::Vector2 StatePlayBooleanWorld::getPlayerPosition() const {
  return getPlayerPhysicalStats().position;
}

float StatePlayBooleanWorld::getPlayerAngle() const {
  return getPlayerPhysicalStats().angle;
}

float StatePlayBooleanWorld::getFloorHeightAt(wp::Vector2 const& pos) const {
  return mWorldData ? mWorldData->getFloorHeight(pos) : 0.0f;
}

float StatePlayBooleanWorld::getCeilingHeightAt(wp::Vector2 const& pos) const {
  return mWorldData ? mWorldData->getCeilingHeight(pos) : 0.0f;
}

float StatePlayBooleanWorld::getPlayerFloorHeight() const {
  auto const& playerStats = getPlayerPhysicalStats();

  return getFloorHeightAt(playerStats.position);
}

float StatePlayBooleanWorld::getPlayerCeilingHeight() const {
  auto const& playerStats = getPlayerPhysicalStats();

  return getCeilingHeightAt(playerStats.position);
}

float StatePlayBooleanWorld::getPlayerLiquidSubmersionDepth() const {
  if (!mWorldData) {
    return 0.0f;
  }

  auto const& playerStats = getPlayerPhysicalStats();
  auto liquidSurface = mWorldData->getLiquidSurfaceHeight(playerStats.position);
  if (!std::isfinite(liquidSurface)) {
    return 0.0f;
  }

  auto submersion = liquidSurface - playerStats.floorZ;
  return std::clamp(submersion, 0.0f, float(BW_PLAYER_HEIGHT));
}

bw::core::LiquidProperties const& StatePlayBooleanWorld::getLiquidPropertiesAt(
    wp::Vector2 const& pos) const {
  return bw::core::GetLiquidProperties(
      mWorldData ? mWorldData->getLiquidType(pos) : bw::core::LiquidType::Water);
}

float StatePlayBooleanWorld::buoyantEquilibriumFraction(
    bw::core::LiquidProperties const& liquid) {
  // A body floats with its own density over the liquid's of itself submerged.
  // A liquid with no density carries nothing, so the player is never held up
  // by it at any depth.
  if (liquid.density <= 0.0f) {
    return std::numeric_limits<float>::infinity();
  }
  return BW_PLAYER_DENSITY / liquid.density;
}

float StatePlayBooleanWorld::getPlayerSwimSpeedMultiplier() const {
  auto const& playerStats = getPlayerPhysicalStats();
  auto submersionFraction =
      getPlayerLiquidSubmersionDepth() / float(BW_PLAYER_HEIGHT);

  // Buoyant force offsets weight in proportion to submerged volume - here
  // approximated by submerged height fraction, since the player's cylinder
  // has a uniform cross-section - reducing the normal force, and so the
  // traction, between feet and floor. Their weight is entirely carried at the
  // submersion the same liquid floats them at, which is exactly where wading
  // gives way to swimming.
  auto tractionFraction =
      1.0f -
      submersionFraction / buoyantEquilibriumFraction(
                               getLiquidPropertiesAt(playerStats.position));
  return std::clamp(
      tractionFraction, float(BW_PLAYER_MIN_SWIM_SPEED_FACTOR), 1.0f);
}

bool StatePlayBooleanWorld::isPlayerSwimming() const {
  if (!mWorldData) {
    return false;
  }

  // Measured against where the player actually is, not the floor far below
  // them: stepping over the edge of a deep pool leaves them briefly in mid-air
  // above its surface, still falling and not yet swimming. Testing the floor
  // instead would flip swimming on at the boundary and snap them down to the
  // surface without a fall.
  return getPlayerLiquidSubmersionDepth() >=
         BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION * float(BW_PLAYER_HEIGHT);
}

bool StatePlayBooleanWorld::tryClimbOutOfLiquid() {
  if (!mWorldData || !mPlayerCollider || !isPlayerSwimming()) {
    return false;
  }

  auto& physicalStats = getPlayerPhysicalStats();
  auto const& position = physicalStats.position;

  auto liquidSurface = mWorldData->getLiquidSurfaceHeight(position);
  if (!std::isfinite(liquidSurface)) {
    return false;
  }

  // Only from the top of the swimmer's reach - the same height the vertical
  // clamp in updatePlayerVerticalPhysics holds them at once they stop rising.
  auto floatHeight = liquidSurface - BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION *
                                         float(BW_PLAYER_HEIGHT);
  if (physicalStats.floorZ < floatHeight - 0.01f) {
    return false;
  }

  if (!bw::app::isLookingUpForLiquidClimb(physicalStats.pitch)) {
    return false;
  }

  auto currentFace = mWorldData->getContainingFaceIndex(position);
  if (currentFace == ~0u) {
    return false;
  }

  auto forward = bw::app::playerMovement({0.0f, 1.0f}, physicalStats.angle);
  auto const& arrangement = mWorldData->getArrangement();
  auto const& walls = mWorldData->getWalls();

  // Pressed against the edge: the collider comes to rest a hair over its own
  // radius from a wall it has run into, so allow a little slack past that.
  auto const contactDistance = float(BW_PLAYER_RADIUS) + 1.0f;

  for (auto wallIndex : mWorldData->getWallsNear(position, contactDistance)) {
    auto const& wall = walls[wallIndex];
    auto const& edge = arrangement.edges[wall.edge];

    auto targetFace = edge.face[0] == currentFace   ? edge.face[1]
                      : edge.face[1] == currentFace ? edge.face[0]
                                                    : ~0u;
    if (targetFace == ~0u || targetFace >= arrangement.faces.size()) {
      continue;
    }

    auto toWorld = [](auto const& vertex) {
      return wp::Vector2{bw::core::arr::ToWorldCoordinate(vertex.x),
                         bw::core::arr::ToWorldCoordinate(vertex.y)};
    };
    auto v0 = toWorld(arrangement.vertices[edge.v[0]]);
    auto v1 = toWorld(arrangement.vertices[edge.v[1]]);
    if (position.distanceToLine(v0, v1) > contactDistance) {
      continue;
    }

    auto closestPoint = position.closestPointOnLine(v0, v1);
    auto outward = closestPoint - position;
    if (outward.normalise() <= 1e-4f) {
      // Standing exactly on the edge leaves no direction to climb towards.
      continue;
    }

    // Facing the floor they mean to climb onto, rather than merely drifting
    // against some other edge of the pool.
    if (!bw::app::isFacingLiquidClimbTarget(forward.dot(outward))) {
      continue;
    }

    auto const& targetProperties =
        arrangement.palette[arrangement.faces[targetFace].paletteIndex];
    // Climbing out is a lift onto something above the swimmer. A floor at or
    // below their float height is just more of the pool - reachable by
    // swimming, and nothing to haul themselves onto. The ledge itself must
    // also be within arm's reach of their eye level.
    if (!bw::app::canClimbOutOfLiquidToFloor(
            physicalStats.floorZ, targetProperties.floorZ)) {
      continue;
    }
    if (targetProperties.ceilingZ - targetProperties.floorZ <
        BW_PLAYER_HEIGHT) {
      continue;
    }

    // The shortest move that puts the whole collider past the edge.
    auto destination =
        closestPoint +
        outward * (float(BW_PLAYER_RADIUS) + BW_PLAYER_CLIMB_OUT_MARGIN);

    // Room to stand there: on the face we meant, and clear of every wall
    // around it - otherwise the climb would end wedged in geometry.
    if (mWorldData->getContainingFaceIndex(destination) != targetFace ||
        mWorldData->circleIntersectsWall(destination, BW_PLAYER_RADIUS) >= 0) {
      continue;
    }

    // Only the horizontal move happens here. Leaving floorZ down at the
    // swimmer's float height hands the rise to the ordinary step-up branch of
    // updatePlayerVerticalPhysics, which climbs towards the new floor at
    // BW_PLAYER_STEP_SPEED from the next frame on - so the eye rises out of
    // the liquid over several frames instead of snapping to the bank. The
    // player is no longer over liquid, so nothing re-enters the swim branch
    // while that plays out.
    physicalStats.position = destination;
    mPlayerCollider->_setPosition(destination);
    mPlayerVerticalVelocity = 0.0f;
    mPlayerSwimEffort = 0.0f;
    return true;
  }

  return false;
}

bw::core::DynamicWorldDataGenerator* StatePlayBooleanWorld::getWDG() {
  auto world = getMap()->getWorld();

  return dynamic_cast<bw::core::DynamicWorldDataGenerator*>(world->getWorldDataGenerator());
}

void StatePlayBooleanWorld::addDisplayMessage(DisplayMessage::Level level, string const& message) {
  mDisplayMessages.push_back({mGlobalTime,
                              level,
                              message});

  bw::common::trimDequeToCapacity(mDisplayMessages, DISPLAY_MESSAGE_COUNT_MAX);
}

void StatePlayBooleanWorld::setupEntities() {
  // if player is not fully in the world, bail
  auto world = getMap()->getWorld();

  auto playerPos = world->getPlayerStartPosition();
  auto playerAngle = bw::core::clamp_angle(world->getPlayerStartAngle());
  auto viewAngle = bw::app::worldViewAngle(playerAngle);

  createEntity((int)EntityType::Player, playerPos, playerAngle, true);

  world->update(0, {playerPos, viewAngle, BW_PLAYER_RADIUS, BW_PLAYER_FOV, BW_PLAYER_VIEW_DISTANCE, false, false, layerSelection()}, {0, 0});

  mWorldData = world->getWorldData();

  if (mWorldData->getContainingFaceIndex(playerPos) == ~0u ||
      mWorldData->circleIntersectsWall(playerPos, BW_PLAYER_RADIUS) >= 0) {
    throw GameException("Player is starting outside the world geometry");
  }
}

void StatePlayBooleanWorld::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) {
  WP_UNUSED(resourceMgr);

  mPlayerPrevAngle = 0;
  mPlayerPrevPitch = 0;
  mPlayerVerticalVelocity = 0.0f;
  mPlayerVerticalHeightInitialized = false;

  auto transitionData = static_cast<applib::StateTransitionData*>(args);

  // Set up objects to pass to next state
  mTransitionData.mapData.prevMap.map = transitionData->mapData.nextMap.map;
  mTransitionData.userData = transitionData->userData;  // WorldRenderer

  mMap = transitionData->mapData.nextMap.map;

  createInput();
  createScreenFxManagement();
  createEntityManagement();

  createCamera();
  createRenderers(renderResourceMgr, transitionData);

  // We want to turn off the default entity rendering from AppLib here, as it is for 2d entities,
  // and these should only be visible in the minimap
  mEntityMgr->setRenderersVisible(false);

  setupScene();
  loadAllReferencedResources();

  // Set up input
  registerInput();

  // For subclasses
  createGameObjects(resourceMgr, renderSystem, renderResourceMgr, args);

  // Start scheduled world clipping
  auto dataGenerator = getWDG();
  assert(dataGenerator && "StatePlayBooleanWorld requires a DynamicWorldDataGenerator");

  mGenerationCallbackToken = dataGenerator->registerGenerationCallback(
      bind(&StatePlayBooleanWorld::handleClippingUpdate, this, std::placeholders::_1));
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  dataGenerator->startGenerationSchedule(model->getGenerationStartInterval());

  // Finish move of transition data
  transitionData->userData = nullptr;
}

void StatePlayBooleanWorld::updatePreInput(float frameTime) {
  VAR_UNUSED(frameTime);

  auto const& physicalStats = getPlayerPhysicalStats();

  mPlayerPrevAngle = physicalStats.angle;
  mPlayerPrevPitch = physicalStats.pitch;
}

void StatePlayBooleanWorld::updatePreEntities(float frameTime) {
  // Uses last frame's settled position/floorZ - this frame's movement (below)
  // has not been computed yet - which is exactly the submersion state that
  // should govern how fast, and by which controls, that movement happens.
  auto entityHandler = static_cast<EntityHandlerBooleanWorld*>(
      applib::ModelInstance::get()->entityHandler.get());
  entityHandler->setSpeedMultiplier(getPlayerSwimSpeedMultiplier());
  entityHandler->setSwimming(isPlayerSwimming());

  // Get input
  wp::Vector2 curPosition, newPosition;
  float curAngle, newAngle;

  getWorldInput(
      &curPosition, &newPosition, &curAngle, &newAngle,
      &mPlayerSwimEffort, frameTime);

  bool playerMoved = newPosition != curPosition;
  bool playerTurned = newAngle != curAngle;

  // Apply to world
  auto world = getMap()->getWorld();

  wp::Vector2 playerPosition;
  float playerAngle;

  playerPosition = newPosition;
  playerAngle = bw::app::worldViewAngle(newAngle);

  world->update(frameTime, {playerPosition, playerAngle, BW_PLAYER_RADIUS, BW_PLAYER_FOV, BW_PLAYER_VIEW_DISTANCE, playerMoved, playerTurned, layerSelection()}, {0, 0});

  mWorldData = world->getWorldData();

  // Supply the physics step with walls around the predicted destination. Player
  // location is evaluated only after that step has resolved movement.
  createWorldCollisions(newPosition);
}

void StatePlayBooleanWorld::updateAudio(float frameTime) {
  BW_UNUSED(frameTime);
}

void StatePlayBooleanWorld::updatePostEntities(float frameTime) {
  auto const& physicalStats = getPlayerPhysicalStats();
  auto location = bw::app::evaluatePlayerLocation(
      *mWorldData, physicalStats.position, BW_PLAYER_RADIUS);
  mPlayerPolygonIndex = location.faceIndex;
  mPlayerBorderIntersectIndex = location.intersectingWallIndex;

  if (!playerInWorld() || playerIntersectsWorldBorders()) {
    // TODO
    // ...
  }

  updatePlayerVerticalPhysics(frameTime);

  // After the vertical step, so a swimmer rising toward the surface is tested
  // at the float height it has just settled them at rather than one frame
  // behind it. A successful climb teleports the player onto another face, so
  // the location read above no longer describes where they are.
  if (tryClimbOutOfLiquid()) {
    auto climbedLocation = bw::app::evaluatePlayerLocation(
        *mWorldData, getPlayerPhysicalStats().position, BW_PLAYER_RADIUS);
    mPlayerPolygonIndex = climbedLocation.faceIndex;
    mPlayerBorderIntersectIndex = climbedLocation.intersectingWallIndex;
  }

  if (mwAudioSystem) {
    updateAudio(frameTime);
  }
}

void StatePlayBooleanWorld::updatePlayerVerticalPhysics(float frameTime) {
  auto& physicalStats = getPlayerPhysicalStats();

  if (!playerInWorld()) {
    // Off the edge of the arrangement entirely (eg. walked through a
    // non-colliding wall) - there is no face to read a floor height from,
    // and getFloorHeightAt would return -infinity here. Freeze in place
    // rather than free-falling forever, so re-entering the world resumes
    // from the height last held while still on a face.
    mPlayerVerticalVelocity = 0.0f;
    return;
  }

  auto targetFloor = getFloorHeightAt(physicalStats.position);

  if (!mPlayerVerticalHeightInitialized) {
    // Before mWorldData exists (very start of map load) the floor query
    // falls back to 0; wait for a real reading before treating any
    // difference as a fall.
    if (!mWorldData) {
      return;
    }
    physicalStats.floorZ = targetFloor;
    mPlayerVerticalVelocity = 0.0f;
    mPlayerVerticalHeightInitialized = true;
    return;
  }

  auto liquidSurface = mWorldData->getLiquidSurfaceHeight(physicalStats.position);
  // The height a floating player settles at. Liquid shallower than the
  // swimming threshold puts this below its own floor, in which case the floor
  // wins and the player wades rather than floats.
  auto floatZ = liquidSurface - BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION *
                                    float(BW_PLAYER_HEIGHT);
  // Submersion is measured at the player's own height, so walking off the edge
  // of a deep pool is an ordinary fall until they actually reach the water.
  // Liquid carries part of the player's weight either where it is deep enough
  // to lift them off the bottom, or while they are still dropping through it
  // after a fall - in both cases gravity alone no longer describes the motion.
  // Standing on the bottom of a shallow pool is neither, and falls through to
  // the ordinary ground logic so wading and steps keep working.
  auto const& liquid = getLiquidPropertiesAt(physicalStats.position);
  auto equilibriumFraction = buoyantEquilibriumFraction(liquid);
  auto equilibriumZ =
      liquidSurface - equilibriumFraction * float(BW_PLAYER_HEIGHT);
  auto inLiquid =
      std::isfinite(liquidSurface) && liquidSurface > physicalStats.floorZ &&
      (equilibriumZ > targetFloor || physicalStats.floorZ > targetFloor);
  if (inLiquid) {
    // Archimedes: the upward force goes with the submerged volume, which for a
    // uniform cylinder is just the submerged fraction of its height. Balanced
    // against weight at the fraction the player floats at, so a resting player
    // feels no net force, a fully submerged one rises, and one barely dipped
    // still falls at close to full gravity.
    auto submergedFraction = std::clamp(
        (liquidSurface - physicalStats.floorZ) / float(BW_PLAYER_HEIGHT), 0.0f,
        1.0f);
    auto buoyantAcceleration =
        BW_PLAYER_GRAVITY * (submergedFraction / equilibriumFraction - 1.0f);

    // Swim input (fly controls, see EntityHandlerBooleanWorld::peekInput) is a
    // force worked against the liquid like any other, not a rate the player is
    // moved at, so the drag below governs it too - a kick builds speed over a
    // few frames and bleeds away again when released, rather than snapping
    // straight to full speed and stopping dead, and the speed it reaches falls
    // out of the liquid's viscosity rather than being stated separately.
    auto swimAcceleration = mPlayerSwimEffort * BW_PLAYER_SWIM_ACCELERATION;

    // Entry speed is carried in rather than discarded: whatever gravity built
    // up on the way down is still here on the first submerged frame, so a
    // plunge from a height drives the player deep before buoyancy returns them
    // to the surface, while stepping in from the bank barely dips them.
    mPlayerVerticalVelocity +=
        (buoyantAcceleration + swimAcceleration) * frameTime;

    // Liquid resists motion through it - what arrests a plunge, holds the rise
    // back to a wallow, and stops the player oscillating about their float
    // height once buoyancy has caught them. Only the submerged part of the
    // player meets that resistance, so the drag eases as they near the surface
    // and the last of a rise accelerates as they break out of the liquid;
    // going the other way, someone dropping in meets little resistance until
    // they are properly under.
    mPlayerVerticalVelocity -=
        mPlayerVerticalVelocity *
        std::min(1.0f, liquid.viscosity * submergedFraction * frameTime);

    auto previousFloorZ = physicalStats.floorZ;
    physicalStats.floorZ += mPlayerVerticalVelocity * frameTime;

    if (physicalStats.floorZ <= targetFloor) {
      // Reached the bottom - a hard stop, but any upward swim input still
      // lifts off again.
      physicalStats.floorZ = targetFloor;
      mPlayerVerticalVelocity = std::max(mPlayerVerticalVelocity, 0.0f);
    }

    // A barrier the player cannot rise through, rather than a ceiling that
    // pulls them down to it: buoyancy and swim input together carry them no
    // higher than their float height (see
    // BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION), but someone dropping in from
    // above starts above that height and must be left to sink past it under
    // their own momentum.
    auto maxFloorZ = std::max(targetFloor, floatZ);
    if (previousFloorZ <= maxFloorZ && physicalStats.floorZ > maxFloorZ) {
      physicalStats.floorZ = maxFloorZ;
      mPlayerVerticalVelocity = std::min(mPlayerVerticalVelocity, 0.0f);
    }
    return;
  }

  if (targetFloor >= physicalStats.floorZ) {
    // Horizontal collision already refused any step too tall to climb (see
    // ArrangementWorldData's step-threshold/clearance rules), so any floor
    // rise reaching here is a walkable step: climb it smoothly rather than
    // snapping straight to it, and stay grounded (no carried fall speed).
    // tryClimbOutOfLiquid deliberately routes through here too, leaving the
    // player below their new bank so this smooths the haul out of the liquid.
    mPlayerVerticalVelocity = 0.0f;
    physicalStats.floorZ += std::min(
        targetFloor - physicalStats.floorZ, BW_PLAYER_STEP_SPEED * frameTime);
  } else {
    // Walked past the edge of the floor beneath us: accelerate downward
    // under gravity until the new, lower floor catches us.
    mPlayerVerticalVelocity -= BW_PLAYER_GRAVITY * frameTime;
    physicalStats.floorZ += mPlayerVerticalVelocity * frameTime;
    if (physicalStats.floorZ <= targetFloor) {
      physicalStats.floorZ = targetFloor;
      mPlayerVerticalVelocity = 0.0f;
    }
  }
}

void StatePlayBooleanWorld::exit() {
  mTransitionData.mapData.prevMap.mapRenderer = mMapRenderer ? move(mMapRenderer) : nullptr;
  mTransitionData.mapData.prevMap.mapCollisionSim = mMapCollisionSim ? move(mMapCollisionSim) : nullptr;
  applib::ModelInstance::get()->collisionSim = nullptr;

  throw wp::application::ReturnFromStateException(&mTransitionData);
}

void StatePlayBooleanWorld::updateActions(vector<string> const& activeStates, float frameTime) {
  VAR_UNUSED(activeStates);
  VAR_UNUSED(frameTime);

  for (auto const& state : activeStates) {
    if (state == "Exit") {
      exit();
    } else if (state == "GenClip") {
      getMap()->getWorld()->generateClipping(true);
    } else if (state == "Debug.Minimap") {
      mDebugDisplay.minimap = !mDebugDisplay.minimap;
    } else if (state == "Debug.CollisionSim") {
      mDebugDisplay.collisionSim = !mDebugDisplay.collisionSim;
    } else if (state == "Debug.ClipGen") {
      mDebugDisplay.clipGeneration = !mDebugDisplay.clipGeneration;
    } else if (state == "Debug.Options") {
      mDebugDisplay.options = !mDebugDisplay.options;
    } else if (state == "ToggleAllLayers") {
      mAllLayers = !mAllLayers;
      getMap()->getWorld()->getWorldDataGenerator()->setLayerSelection(
          layerSelection());
    } else if (state == "RenderGraphCapture") {
      mRenderGraphCaptureRequested = true;
    } else if (state == "Screenshot") {
      mScreenshotRequested = true;
    }
  }

  mEntityMgr->setRenderersVisible(false);
}

void StatePlayBooleanWorld::updatePreRenderers(float frameTime) {
  auto viewBounds = getViewBounds();

  // Set camera position
  auto const& physicalStats = getPlayerPhysicalStats();

  // Camera position is the player's current simulated height (see
  // updatePlayerVerticalPhysics - smoothed onto steps, falling under
  // gravity off ledges) plus player eye height, not the floor directly
  // beneath them: those two only match once physics has caught up.
  auto playerViewHeight = physicalStats.floorZ + BW_PLAYER_EYE_HEIGHT;

  static_cast<ReactiveCamera*>(mCamera3d.get())->setPosition({physicalStats.position.x, playerViewHeight, -physicalStats.position.y});
  // Renderer and authored yaw now increase in the same direction.
  static_cast<ReactiveCamera*>(mCamera3d.get())->yaw(physicalStats.angle - mPlayerPrevAngle);
  static_cast<ReactiveCamera*>(mCamera3d.get())->pitch(physicalStats.pitch - mPlayerPrevPitch);

  // World 3d uses the handedness-preserving mapping (X, elevation, -Y).
  // Move the light horizontally from the player's eye along the current yaw;
  // pitch does not affect it.
  auto lightOffset = Vector2::fromAngle(
                         bw::app::worldViewAngle(physicalStats.angle), Clockwise) *
                     mDebugDisplay.lightDistance;
  glm::vec3 playerPosition{
      physicalStats.position.x,
      playerViewHeight,
      -physicalStats.position.y};
  glm::vec3 lightPosition{
      playerPosition.x + lightOffset.x,
      playerPosition.y,
      playerPosition.z - lightOffset.y};
  updatePlayerTorchMarker(lightPosition);
  auto const domainName = std::string(bw::app::playerTorchShadowDomain);
  auto const& sessionShadows = mDebugDisplay.playerTorchShadows;
  auto desiredOptions = bw::app::playerTorchMppShadowOptions(
      sessionShadows.options, lightPosition, sessionShadows.enabledOverride);
  // MPP turns enabled off after logging a hardware/allocation fallback. Detect
  // that only after a previously enabled request, so forcing on a configured-
  // off domain still gets its first hardware attempt. Once detected, neither
  // the configured value nor F5's override may retry around that fallback.
  if (mwRenderSystem->hasShadowDomain(domainName) &&
      bw::app::playerTorchShadowHardwareFallbackDetected(
          mPlayerTorchShadowRequestedEnabled, desiredOptions.enabled,
          mwRenderSystem->getShadowDomainOptions(domainName).enabled)) {
    mPlayerTorchShadowHardwareFallback = true;
  }
  if (!mPlayerTorchShadowHardwareFallback) {
    mwRenderSystem->configureShadowDomain(domainName, desiredOptions);
  }
  mPlayerTorchShadowRequestedEnabled = desiredOptions.enabled;
  mwRenderer->update(
      getMap()->getWorld(), *mWorldData, playerPosition, lightPosition,
      mDebugDisplay.playerTorch, mDebugDisplay.liquidOpacityOverride,
      mDebugDisplay.liquidTintOverride,
      mDebugDisplay.liquidReflectanceOverride,
      mDebugDisplay.liquidF0Override,
      mDebugDisplay.liquidReflectionMipLevel,
      mDebugDisplay.liquidReflectionEnabled,
      mDebugDisplay.sortGeometryFrontToBack, gNoMaterialOverride,
      gNoMaterialOverride, gAuthoredMaterialScale,
      mDebugDisplay.pixelSize, gNoSecondaryMaterial, frameTime);
}

void StatePlayBooleanWorld::suspendImpl(void* args) {
  VAR_UNUSED(args);
}

void StatePlayBooleanWorld::resumeImpl(void* args) {
  if (args) {
    bool const* shouldExit = static_cast<bool const*>(args);
    mExitScheduled = *shouldExit;
  }
}

void StatePlayBooleanWorld::handleClippingUpdate(bw::core::DynamicWorldDataGenerator::GenerationDetails const& details) {
  lock_guard<mutex> lock(mClippingRecordsMutex);

  // If state is Generating, insert
  switch (details.state) {
    case bw::core::DynamicWorldDataGenerator::GenerationState::Generating:
      mClippingRecords.push_back({details.clippingId,
                                  mGlobalTime,
                                  -1.0,
                                  -1.0,
                                  details.genTimeNs,
                                  details.stats});
      break;

    case bw::core::DynamicWorldDataGenerator::GenerationState::Committed:
      mwRenderer->setWorldChanged();
      // World models use dynamic providers, so MPP cannot observe their
      // vertex/index writes through model revisions. A committed generation is
      // the explicit shadow-relevant geometry boundary.
      if (mwRenderSystem->hasShadowDomain(
              std::string(bw::app::playerTorchShadowDomain))) {
        mwRenderSystem->invalidateShadowDomain(
            std::string(bw::app::playerTorchShadowDomain));
      }
      addDisplayMessage(
          DisplayMessage::Level::Debug,
          format(
              "Arrangement committed: {} vertices, {} faces",
              details.stats.arrangement.vertexCount,
              details.stats.arrangement.faceCount));
      [[fallthrough]];
    case bw::core::DynamicWorldDataGenerator::GenerationState::Generated:
      // Find the record with the matching ID and update
      for (auto& record : mClippingRecords) {
        if (record.clippingId == details.clippingId) {
          if (details.state == bw::core::DynamicWorldDataGenerator::GenerationState::Generated) {
            record.generationCompleteTime = mGlobalTime;
            record.generationTimeNs = details.genTimeNs;
            record.stats = details.stats;
          } else {
            record.commitedTime = mGlobalTime;
          }

          break;
        }
      }
      break;

    default:
      break;
  }

  bw::common::trimDequeToCapacity(mClippingRecords, CLIPPING_RECORD_COUNT_MAX);
}

void StatePlayBooleanWorld::updateImpl(float frameTime) {
  mGlobalTime += frameTime;

  if (mExitScheduled) {
    exit();
  }

  updatePreInput(frameTime);
  updateInput(frameTime);
  updatePreEntities(frameTime);
  updateEntityManagement(frameTime);
  updatePostEntities(frameTime);
  updateCamera(frameTime);
  updatePreRenderers(frameTime);
  updateScreenFxManagement(frameTime);
  updateRenderers(frameTime);
}

// Draws the 3d world through the world renderer's offscreen target and
// composites that target across the whole screen (ADR 0012). Everything drawn
// after this call - HUD messages, the debug panel, ImGui - lands on the screen
// at native resolution.
//
// The scene is handed to an MPP render-graph pipeline, which applies the
// selected MSAA or FXAA stage. This state copies the filtered output into the
// selected world target and composites it to the actual screen.
void StatePlayBooleanWorld::saveScreenshot(
    mpp::RenderSystem* renderSystem) {
  namespace fs = std::filesystem;
  using namespace std::chrono;

  auto const width = renderSystem->getWindowWidth();
  auto const height = renderSystem->getWindowHeight();
  if (width == 0 || height == 0) {
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Could not save screenshot: the window has no drawable area.");
    return;
  }

  try {
    std::vector<uint8_t> pixels(width * height * 3);
    GLint previousPackAlignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
        GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    auto const rowSize = width * 3;
    for (size_t y = 0; y < height / 2; ++y) {
      auto top = pixels.begin() + y * rowSize;
      auto bottom = pixels.begin() + (height - y - 1) * rowSize;
      std::swap_ranges(top, top + rowSize, bottom);
    }

    auto const now = system_clock::now();
    auto const time = system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    auto const millisecondsPart =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream filename;
    filename << "BooleanWorld_"
             << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S-")
             << std::setfill('0') << std::setw(3) << millisecondsPart
             << ".png";

    auto const shotsDirectory = fs::current_path() / "shots";
    fs::create_directories(shotsDirectory);
    auto const filepath = shotsDirectory / filename.str();

    utils::Image image;
    image.loadFromData(width, height, 24, pixels.data());
    image.saveToFile(filepath.string());
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Saved screenshot to " + filepath.string());
  } catch (std::exception const& exception) {
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Could not save screenshot: " + std::string(exception.what()));
  }
}

void StatePlayBooleanWorld::saveRenderGraphImages(
    std::vector<mpp::GraphImageCapture> const& captures,
    std::vector<mpp::GraphPassExecutionStats> const& passStats) {
  namespace fs = std::filesystem;
  using namespace std::chrono;

  try {
    auto const now = system_clock::now();
    auto const time = system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    auto const millisecondsPart =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream directoryName;
    directoryName << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S-")
                  << std::setfill('0') << std::setw(3) << millisecondsPart;
    auto const directory = fs::current_path() / "shots" / directoryName.str();
    fs::create_directories(directory);

    std::map<std::string, size_t> passOutputCounts;
    for (auto const& capture : captures) {
      ++passOutputCounts[capture.passName];
    }
    auto sanitize = [](std::string name) {
      for (auto& character : name) {
        auto const value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '-' && character != '_') {
          character = '_';
        }
      }
      return name;
    };
    {
      std::ofstream manifest(directory / "render-graph.txt");
      for (auto const& stats : passStats) {
        manifest << stats.name << ": triangles=" << stats.trianglesSubmitted
                 << ", primitives=" << stats.primitivesSubmitted
                 << ", fullscreen-quads=" << stats.fullscreenQuads;
        if (!stats.primaryColourOutputName.empty()) {
          manifest << ", image=" << stats.primaryColourOutputName
                   << ", dimensions=" << stats.primaryColourOutputWidth << 'x'
                   << stats.primaryColourOutputHeight;
        }
        manifest << '\n';
      }
    }
    for (size_t index = 0; index < captures.size(); ++index) {
      auto const& capture = captures[index];
      if (capture.width == 0 || capture.height == 0 ||
          capture.pixels.empty()) {
        continue;
      }
      std::ostringstream filename;
      filename << std::setfill('0') << std::setw(2) << index << '_'
               << sanitize(capture.passName);
      if (passOutputCounts[capture.passName] > 1) {
        filename << "--" << sanitize(capture.imageName);
      }
      if (capture.cubeFace != mpp::GraphNoCubeFace) {
        filename << "--face-" << capture.cubeFace;
      }
      filename << ".png";

      utils::Image image;
      image.loadFromData(
          capture.width, capture.height, 24, capture.pixels.data());
      image.saveToFile((directory / filename.str()).string());
    }
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Saved " + std::to_string(captures.size()) +
            " render-graph images to " + directory.string());
  } catch (std::exception const& exception) {
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Could not save render-graph images: " +
            std::string(exception.what()));
  }
}

void StatePlayBooleanWorld::renderWorldThroughTarget(mpp::RenderSystem* renderSystem) {
  auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
  auto renderScale = model->getActiveRenderScale();
  auto antiAliasing = model->getActiveAntiAliasing();
  auto const& worldTarget = mwRenderer->getRenderTarget(renderScale);
  auto technique = model->getWaterReflectionTechnique();
  if (!mLiquidReflectionSelectionTechnique ||
      *mLiquidReflectionSelectionTechnique != technique) {
    mLiquidReflectionSelection.reset();
    mLiquidReflectionSelectionTechnique = technique;
  }
  std::vector<mpp::PlanarReflectionPlaneDescriptor> planarPlanes;
  if (!mDebugDisplay.fragmentOverdraw && mWorldData &&
      technique == bw::app::WaterReflectionTechnique::Planar) {
    planarPlanes = discoverLiquidReflectionPlanes(
        *mWorldData, *mCamera3d, mLiquidReflectionSelection);
  }
  auto const& pipeline = mDebugDisplay.fragmentOverdraw
                             ? getOrCreateFragmentOverdrawPipeline(renderScale)
                             : getOrCreateWorldRenderPipeline(
                                   renderScale, antiAliasing, planarPlanes);

  // These only change the world's two scene models; entities and debug UI
  // remain filled. Applying it here also carries an enabled debug option onto
  // a newly created map renderer.
  mwRenderer->setWireframe(mDebugDisplay.wireframe);
  mwRenderer->setFragmentOverdraw(mDebugDisplay.fragmentOverdraw);
  // MPP orders every 3D draw command while WorldRenderer orders the triangles
  // inside each material command. Together these exercise the complete
  // closest-first diagnostic path for this scene.
  renderSystem->setSortGeometryFrontToBack(
      mDebugDisplay.sortGeometryFrontToBack);

  // The graph pipeline renders at the target's dimensions and applies its
  // selected AA stage. The camera retains the window aspect ratio, and
  // the final composite stretches this target over that same window.
  mScene->setViewport(0, 0, worldTarget->getWidth(), worldTarget->getHeight());
  auto const captureRenderGraph = mRenderGraphCaptureRequested;
  mRenderGraphCaptureRequested = false;
  if (captureRenderGraph) {
    pipeline->requestGraphImageCapture();
  }
  renderSystem->renderScene(
      mScene, mCamera3d, {0.0f, 0.0f}, pipeline->getName());
  if (pipeline->planarReflectionRuntimeFailed() &&
      !mPlanarReflectionSessionFailed) {
    mPlanarReflectionSessionFailed = true;
    addDisplayMessage(
        DisplayMessage::Level::Game,
        "Planar Water reflection disabled for this session; the selected "
        "technique remains Planar: " +
            pipeline->getPlanarReflectionFailureMessage());
  }
  if (captureRenderGraph) {
    saveRenderGraphImages(
        pipeline->takeGraphImageCaptures(),
        pipeline->getLastGraphExecutionStats());
  }

  // The named output is always the final offscreen shaded image, addressed by
  // its position among the pipeline's graph images. Every image MPP creates
  // ahead of it shifts that position: generated water appends the resolved
  // scene copy and WaterComposite, AO adds three, MRT-normal GTAO also
  // inserts two scene attachments, an active shadow domain inserts its
  // imported depth image between the scene depth and the AO images, and the
  // scene extra outputs are created before all of those. Miscounting does not
  // fail cleanly - it silently addresses a different image, and presenting one
  // with no colour attachment (the imported shadow cube) crashes in
  // Texture::bind on an empty texture list. Derive the extras count from the
  // same function the pipeline is built from rather than restating it.
  auto ambientOcclusionEnabled =
      !mDebugDisplay.fragmentOverdraw &&
      mDebugDisplay.ambientOcclusionEnabled &&
      mDebugDisplay.ambientOcclusion != bw::app::AmbientOcclusion::None;
  auto usesMrtNormals =
      ambientOcclusionEnabled &&
      mDebugDisplay.ambientOcclusion == bw::app::AmbientOcclusion::GtaoNormals;
  auto activeShadowImage =
      !mDebugDisplay.fragmentOverdraw &&
      renderSystem->getShadowDomainDepthTarget(
          std::string(bw::app::playerTorchShadowDomain)) != nullptr;
  auto sceneExtraOutputCount =
      ambientOcclusionEnabled
          ? static_cast<std::uint32_t>(
                worldSceneExtraOutputs(usesMrtNormals).size())
          : 0u;
  auto preWaterOutputImage = ambientOcclusionEnabled
                                 ? (usesMrtNormals ? 6u : 4u) +
                                       sceneExtraOutputCount +
                                       (activeShadowImage ? 1u : 0u)
                                 : 0u;
  // Without AO, SceneDepth (and optionally the shadow import) sits between
  // SceneLdr and the generated-water images. With AO, those earlier images are
  // already included in preWaterOutputImage, so only the technique-specific
  // reflection images and WaterComposite remain to be added.
  auto screenSpaceWater =
      technique == bw::app::WaterReflectionTechnique::ScreenSpace;
  auto planarRequested =
      technique == bw::app::WaterReflectionTechnique::Planar &&
      !planarPlanes.empty();
  auto planarWater = planarRequested && !mPlanarReflectionSessionFailed;
  auto failedPlanarWater = planarRequested && mPlanarReflectionSessionFailed;
  auto outputImage = preWaterOutputImage;
  if (mDebugDisplay.fragmentOverdraw) {
    outputImage = preWaterOutputImage;
  } else if (ambientOcclusionEnabled) {
    // Each Planar plane declares its colour and depth images before the opaque
    // and AO images, then WaterComposite follows the final AO image.
    outputImage =
        preWaterOutputImage +
        (planarWater        ? 2u * static_cast<std::uint32_t>(planarPlanes.size()) + 1u
         : failedPlanarWater ? 1u
         : screenSpaceWater  ? 2u
                             : 0u);
  } else if (screenSpaceWater) {
    outputImage = 3u + (activeShadowImage ? 1u : 0u);
  } else if (planarWater) {
    outputImage = 2u +
                  2u * static_cast<std::uint32_t>(planarPlanes.size()) +
                  (activeShadowImage ? 1u : 0u);
  } else if (failedPlanarWater) {
    outputImage = 2u + (activeShadowImage ? 1u : 0u);
  } else {
    outputImage = 0u;
  }
  auto sceneTarget = pipeline->getGraphImageRenderTarget({outputImage, 1});
  assert(sceneTarget);
  auto sceneTexture = static_cast<mpp::RenderTexture*>(sceneTarget.get());
  auto worldTexture = static_cast<mpp::RenderTexture*>(worldTarget.get());

  // MPP's fullscreen quad has window-sized geometry, so scale it to the world
  // target before drawing. This keeps half and quarter render scales from
  // clipping the source to only part of the view.
  renderSystem->pushRenderTarget(worldTarget);
  renderSystem->resetViewport();
  renderSystem->clearScreen(mpp::Colour::Black);
  renderSystem->setProjection2dOrthographic();
  renderSystem->resetTransform();
  renderSystem->scaleTransform2d({static_cast<float>(worldTarget->getWidth()) / renderSystem->getWindowWidth(),
                                  static_cast<float>(worldTarget->getHeight()) / renderSystem->getWindowHeight()});
  if (mDebugDisplay.fragmentOverdraw) {
    // Remove the one-fragment baseline while resolving: only additional
    // fragments are overdraw, so a successful depth prepass resolves black.
    renderSystem->renderGraphFullscreen(
        mFragmentOverdrawResolveProgram, {{"TEX1", sceneTexture}}, {});
  } else {
    // Apply BooleanWorld's data-driven vignette as the final world post-process.
    // HUD and debug UI are drawn later and therefore remain unaffected.
    mpp::UniformCollection vignetteParameters;
    vignetteParameters.setUniform(
        "VIGNETTE_COLOUR",
        glm::vec3{
            mDebugDisplay.vignetteColour[0],
            mDebugDisplay.vignetteColour[1],
            mDebugDisplay.vignetteColour[2]});
    vignetteParameters.setUniform(
        "VIGNETTE_STRENGTH", mDebugDisplay.vignetteStrength);
    vignetteParameters.setUniform(
        "VIGNETTE_INNER_RADIUS", mDebugDisplay.vignetteInnerRadius);
    vignetteParameters.setUniform(
        "VIGNETTE_FALLOFF_WIDTH", mDebugDisplay.vignetteFalloffWidth);
    renderSystem->renderGraphFullscreen(
        mVignetteProgram, {{"TEX1", sceneTexture}}, vignetteParameters);
  }
  renderSystem->popRenderTarget();

  // Composite the resolved world across the screen. The blend factors are set
  // explicitly so the composite replaces the screen rather than being tinted
  // or blended by
  // whatever state the scene left behind.
  renderSystem->resetViewport();
  renderSystem->clearScreen(mpp::Colour::Black);
  renderSystem->setProjection2dOrthographic();
  renderSystem->resetTransform();
  renderSystem->renderFullscreenQuad(
      worldTexture, mpp::BlendMode::One, mpp::BlendMode::Zero);
}

void StatePlayBooleanWorld::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(resourceMgr);

  // Screen FX setup
  // mScreenFxMgr->preRender(getViewCentreWorldPosition());

  renderWorldThroughTarget(renderSystem);

  // Render post-effects
  // mScreenFxMgr->postRender(renderSystem);

  // Messages
  auto numMessages = (int)mDisplayMessages.size();
  auto messageIndex = max(numMessages - 4, 0);

  int y = 0;
  for (int i = messageIndex; i < numMessages; ++i) {
    auto const& message = mDisplayMessages[i];

    if (((message.time + DISPLAY_MESSAGE_TIME) < mGlobalTime) ||
        (int)message.level < (int)gDisplayMessageLevel) {
      continue;
    }

    auto colour = message.level == DisplayMessage::Level::Debug ? mpp::Colour::Grey75 : mpp::Colour::White;

    renderSystem->renderText(format("{:.2f}", message.time), 0, y, colour);
    renderSystem->renderText(message.text, 100, y, colour);
    y += 16;
  }

  if (mScreenshotRequested) {
    mScreenshotRequested = false;
    renderSystem->flushVertexBuffers();
    saveScreenshot(renderSystem);
  }
}

bool StatePlayBooleanWorld::_imGuiActive() const {
  return mDebugDisplay.active();
}

bool StatePlayBooleanWorld::_imGuiCapturesInput() const {
  // The input panel is dragged with the mouse, so it needs the cursor - which
  // means view control stops while it is up, and the mouse can be let go of
  // over the slider without turning the player.
  return mDebugDisplay.clipGeneration || mDebugDisplay.options;
}

ImVec2 StatePlayBooleanWorld::wpVecToImVec2(wp::Vector2 const& v, wp::Vector2 const& offset, wp::Vector2 const& size, wp::Vector2 const& scale) {
  auto position = bw::app::minimapPosition(v, offset, size, scale);
  return {position.x, position.y};
}

void StatePlayBooleanWorld::ImGui_renderArrangement(bw::core::ArrangementWorldData const& worldData, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList) {
  VAR_UNUSED(viewBounds);
  auto const& arrangement = worldData.getArrangement();
  auto toWorld = [&](uint32_t index) {
    auto const& vertex = arrangement.vertices[index];
    return wp::Vector2{
        bw::core::arr::ToWorldCoordinate(vertex.x),
        bw::core::arr::ToWorldCoordinate(vertex.y)};
  };
  drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
  for (auto const& triangle : worldData.getTriangles()) {
    auto v0 = toWorld(triangle.v[0]);
    auto v1 = toWorld(triangle.v[1]);
    auto v2 = toWorld(triangle.v[2]);
    drawList->AddTriangleFilled(
        wpVecToImVec2(v0, viewOffset, viewSize, viewScale),
        wpVecToImVec2(v1, viewOffset, viewSize, viewScale),
        wpVecToImVec2(v2, viewOffset, viewSize, viewScale),
        gImGui_MapBackgroundColour);
    if (mDebugDisplay._renderTriangulationLines) {
      drawList->AddTriangle(
          wpVecToImVec2(v0, viewOffset, viewSize, viewScale),
          wpVecToImVec2(v1, viewOffset, viewSize, viewScale),
          wpVecToImVec2(v2, viewOffset, viewSize, viewScale),
          gImGui_TriangulationLineColour);
    }
  }
  for (auto const& wall : worldData.getWalls()) {
    if (wall.kind != bw::core::arr::ArrangementWallKind::Border) {
      continue;
    }
    auto const& edge = arrangement.edges[wall.edge];
    drawList->AddLine(
        wpVecToImVec2(toWorld(edge.v[0]), viewOffset, viewSize, viewScale),
        wpVecToImVec2(toWorld(edge.v[1]), viewOffset, viewSize, viewScale),
        gImGui_MapBorderColour,
        2.0f);
  }
}

void StatePlayBooleanWorld::ImGui_renderView(vector<wp::Vector2> const& viewVertices, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList) {
  VAR_UNUSED(viewBounds);

  // View cone
  auto numVertices = (uint32_t)viewVertices.size();
  for (uint32_t i = 0; i < numVertices; ++i) {
    uint32_t j = (i + 1) % numVertices;

    drawList->AddLine(
        wpVecToImVec2(viewVertices[i], viewOffset, viewSize, viewScale),
        wpVecToImVec2(viewVertices[j], viewOffset, viewSize, viewScale),
        gImGui_ViewAreaColour,
        2.0f);
  }

  ImVec2 playerPos = wpVecToImVec2(viewVertices[0], viewOffset, viewSize, viewScale);

  // View radius
  drawList->AddCircleFilled(playerPos, bw::app::minimapRadius(BW_PLAYER_VIEW_DISTANCE, viewScale), ImColor(0.5f, 0.8f, 0.5f, 0.25f));

  // Player circle
  drawList->AddCircleFilled(playerPos, bw::app::minimapRadius(BW_PLAYER_RADIUS, viewScale), ImColor(0.8f, 0.8f, 0.4f));
}

void StatePlayBooleanWorld::debug_renderMinimap(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList) {
  if (!mDebugDisplay.minimap) {
    return;
  }

  auto const& player = getPlayerPhysicalStats();
  auto viewAngle = bw::app::worldViewAngle(player.angle);
  auto const [v1, v2] = bw::core::calculateFovTriangle(
      player.position, viewAngle, BW_PLAYER_VIEW_DISTANCE, BW_PLAYER_FOV);
  vector<wp::Vector2> viewVertices{player.position, v1, v2};

  // Draw only the folded arrangement. Authored Primitive contours can extend
  // beyond the resulting level (especially after boolean operations), so
  // overlaying them here makes the minimap look like it contains hulls or
  // other geometry that is not actually playable.
  ImGui_renderArrangement(*mWorldData, viewBounds, viewOffset, viewSize, viewScale, drawList);
  ImGui_renderView(viewVertices, viewBounds, viewOffset, viewSize, viewScale, drawList);
}

void StatePlayBooleanWorld::debug_renderCollisionSim(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, ImDrawList* drawList) {
  if (!mDebugDisplay.collisionSim) {
    return;
  }

  // Internal walls - the floor and ceiling steps that run between two open
  // faces - never reach the collision sim: the wall grid holds only what
  // blocks the player, so a step shallow enough to walk up is missing from
  // the lines below. Draw them first and thinner, in a lighter colour, so a
  // blocking wall drawn over the top of one still reads as solid.
  //
  // They cover the whole view rather than the patch the collision lines are
  // gathered from: they are the context those lines sit in, and on the test
  // map the nearest one to the player at spawn is already outside that patch.
  // ImGui clips whatever falls off screen, as it does for the minimap's walls.
  if (mWorldData) {
    auto const& arrangement = mWorldData->getArrangement();

    // Walls are not indexed by kind, so this walks all of them. It runs only
    // while the overlay is up.
    for (auto const& wall : mWorldData->getWalls()) {
      if (wall.kind == bw::core::arr::ArrangementWallKind::Border) {
        continue;
      }

      auto const& edge = arrangement.edges[wall.edge];
      auto const& fixed0 = arrangement.vertices[edge.v[0]];
      auto const& fixed1 = arrangement.vertices[edge.v[1]];
      wp::Vector2 v0{
          bw::core::arr::ToWorldCoordinate(fixed0.x),
          bw::core::arr::ToWorldCoordinate(fixed0.y)};
      wp::Vector2 v1{
          bw::core::arr::ToWorldCoordinate(fixed1.x),
          bw::core::arr::ToWorldCoordinate(fixed1.y)};

      drawList->AddLine(
          wpVecToImVec2(v0, viewOffset, viewSize, viewScale),
          wpVecToImVec2(v1, viewOffset, viewSize, viewScale),
          gImGui_CollisionLine2WayColour,
          1.5f);
    }
  }

  auto const& lines = mWorldCollisionSim->getLines();
  for (auto const& line : lines) {
    auto const& v0 = line.getVertex(0);
    auto const& v1 = line.getVertex(1);
    drawList->AddLine(
        wpVecToImVec2(v0, viewOffset, viewSize, viewScale),
        wpVecToImVec2(v1, viewOffset, viewSize, viewScale),
        gImGui_CollisionLineSolidColour,
        2.5f);
  }

  // Player circle
  ImVec2 playerPos = wpVecToImVec2(getPlayerPosition(), viewOffset, viewSize, viewScale);
  drawList->AddCircleFilled(playerPos, BW_PLAYER_RADIUS, ImColor(0.8f, 0.8f, 0.4f));
}

void StatePlayBooleanWorld::debug_renderClipGenerationInfo(ImDrawList* drawList) {
  VAR_UNUSED(drawList);

  if (!mDebugDisplay.clipGeneration) {
    return;
  }

  if (ImGui::Begin("Clipping records")) {
    auto model =
        static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
    auto generationStartInterval = model->getGenerationStartInterval();
    if (ImGui::SliderFloat(
            "Generation start interval", &generationStartInterval,
            0.0f, 30.0f, "%.2f s")) {
      model->setGenerationStartInterval(generationStartInterval);
      getWDG()->setGenerationStartInterval(generationStartInterval);
    }
    ImGui::TextDisabled(
        "F4 session-only; zero restarts after each Generation completes.");
    ImGui::Separator();

    vector<ClippingRecord> records;
    {
      lock_guard<mutex> lock(mClippingRecordsMutex);
      records.assign(mClippingRecords.begin(), mClippingRecords.end());
    }

    constexpr double TimelineDuration = 5.0;
    constexpr float TimelineRowHeight = 22.0f;
    constexpr float TimelineAxisHeight = 24.0f;
    auto timelineEnd = mGlobalTime;
    auto timelineStart = timelineEnd - TimelineDuration;

    vector<ClippingRecord const*> visibleRecords;
    for (auto const& record : records) {
      auto lastEventTime = record.commitedTime >= 0.0
                               ? record.commitedTime
                           : record.generationCompleteTime >= 0.0
                               ? record.generationCompleteTime
                               : timelineEnd;
      if (lastEventTime >= timelineStart &&
          record.generationStartedTime <= timelineEnd) {
        visibleRecords.push_back(&record);
      }
    }

    ImGui::TextUnformatted("Last 5 seconds");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f), "Start / generating");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "End");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.40f, 1.0f), "Commit");

    auto canvasPosition = ImGui::GetCursorScreenPos();
    auto canvasWidth = max(ImGui::GetContentRegionAvail().x, 320.0f);
    auto rowCount = max(size_t(visibleRecords.size()), size_t(1));
    auto canvasHeight = TimelineAxisHeight + TimelineRowHeight * rowCount;
    ImGui::InvisibleButton(
        "##GenerationTimeline", {canvasWidth, canvasHeight});

    auto timelineDrawList = ImGui::GetWindowDrawList();
    auto canvasEnd = ImVec2{
        canvasPosition.x + canvasWidth,
        canvasPosition.y + canvasHeight};
    timelineDrawList->AddRectFilled(
        canvasPosition, canvasEnd, IM_COL32(24, 26, 31, 255), 3.0f);

    constexpr float LabelWidth = 52.0f;
    auto graphStartX = canvasPosition.x + LabelWidth;
    auto graphEndX = canvasEnd.x - 8.0f;
    auto timeToX = [&](double time) {
      auto fraction = clamp(
          (time - timelineStart) / TimelineDuration, 0.0, 1.0);
      return graphStartX +
             static_cast<float>(fraction) * (graphEndX - graphStartX);
    };

    for (int second = 0; second <= int(TimelineDuration); ++second) {
      auto x = graphStartX +
               (graphEndX - graphStartX) *
                   (static_cast<float>(second) / float(TimelineDuration));
      timelineDrawList->AddLine(
          {x, canvasPosition.y + TimelineAxisHeight - 5.0f},
          {x, canvasEnd.y},
          IM_COL32(65, 69, 78, 255));
      auto label = second == int(TimelineDuration)
                       ? string("now")
                       : format("-{}s", int(TimelineDuration) - second);
      timelineDrawList->AddText(
          {x - 10.0f, canvasPosition.y + 3.0f},
          IM_COL32(180, 184, 194, 255), label.c_str());
    }

    if (visibleRecords.empty()) {
      timelineDrawList->AddText(
          {graphStartX + 8.0f,
           canvasPosition.y + TimelineAxisHeight + 3.0f},
          IM_COL32(150, 154, 164, 255),
          "No generation activity");
    }

    for (size_t row = 0; row < visibleRecords.size(); ++row) {
      auto const& record = *visibleRecords[visibleRecords.size() - 1 - row];
      auto y = canvasPosition.y + TimelineAxisHeight +
               TimelineRowHeight * (static_cast<float>(row) + 0.5f);
      auto idLabel = format("#{}", record.clippingId);
      timelineDrawList->AddText(
          {canvasPosition.x + 6.0f, y - 7.0f},
          IM_COL32(205, 208, 216, 255), idLabel.c_str());

      auto generationEnd = record.generationCompleteTime >= 0.0
                               ? record.generationCompleteTime
                               : timelineEnd;
      if (generationEnd >= timelineStart &&
          record.generationStartedTime <= timelineEnd) {
        auto startX = timeToX(record.generationStartedTime);
        auto endX = timeToX(generationEnd);
        timelineDrawList->AddLine(
            {startX, y}, {endX, y}, IM_COL32(242, 166, 51, 255), 6.0f);
        if (record.generationStartedTime >= timelineStart) {
          timelineDrawList->AddCircleFilled(
              {startX, y}, 4.0f, IM_COL32(242, 166, 51, 255));
        }
        if (record.generationCompleteTime >= timelineStart &&
            record.generationCompleteTime <= timelineEnd) {
          timelineDrawList->AddCircleFilled(
              {endX, y}, 4.0f, IM_COL32(64, 191, 255, 255));
        }
      }

      if (record.commitedTime >= timelineStart &&
          record.commitedTime <= timelineEnd) {
        auto x = timeToX(record.commitedTime);
        timelineDrawList->AddQuadFilled(
            {x, y - 6.0f}, {x + 6.0f, y},
            {x, y + 6.0f}, {x - 6.0f, y},
            IM_COL32(76, 230, 102, 255));
      }
    }

    ImGui::Spacing();

    ImGuiTableFlags flags =
        ImGuiTableFlags_SizingStretchSame |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV |
        ImGuiTableFlags_ContextMenuInBody;

    if (ImGui::BeginTable("Generation", 18, flags)) {
      ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 128);
      ImGui::TableSetupColumn("Gen 0", ImGuiTableColumnFlags_WidthFixed, 128);
      ImGui::TableSetupColumn("Gen 1", ImGuiTableColumnFlags_WidthFixed, 128);
      ImGui::TableSetupColumn("Commit", ImGuiTableColumnFlags_WidthFixed, 128);
      ImGui::TableSetupColumn("Lag (s)", ImGuiTableColumnFlags_WidthFixed, 96);
      ImGui::TableSetupColumn("Gen (us)", ImGuiTableColumnFlags_WidthFixed, 96);
      ImGui::TableSetupColumn("< p");
      ImGui::TableSetupColumn("< p:vis");
      ImGui::TableSetupColumn("< p:upd");
      ImGui::TableSetupColumn("Verts");
      ImGui::TableSetupColumn("Edges");
      ImGui::TableSetupColumn("Faces");
      ImGui::TableSetupColumn("Tris");
      ImGui::TableSetupColumn("Walls");
      ImGui::TableSetupColumn("Chips");
      ImGui::TableSetupColumn("Wedges");
      ImGui::TableSetupColumn("PSLG (us)");
      ImGui::TableSetupColumn("Classify (us)");
      ImGui::TableHeadersRow();

      auto numRecords = records.size();
      auto recordsToShow = min(size_t(10), numRecords);
      for (size_t i = 0; i < recordsToShow; ++i) {
        auto const& record = records[numRecords - 1 - i];

        ImGui::TableNextRow();

        // Id
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", record.clippingId);

        // Gen started
        ImGui::TableSetColumnIndex(1);

        if (record.generationStartedTime >= 0.0) {
          ImGui::Text("%5.4f", record.generationStartedTime);
        } else {
          ImGui::Text("-");
        }

        // Gen complete
        ImGui::TableSetColumnIndex(2);

        if (record.generationCompleteTime >= 0.0) {
          ImGui::Text("%5.4f", record.generationCompleteTime);
        } else {
          ImGui::Text("-");
        }

        // Commit complete
        ImGui::TableSetColumnIndex(3);

        if (record.commitedTime >= 0.0) {
          ImGui::Text("%5.4f", record.commitedTime);
        } else {
          ImGui::Text("-");
        }

        // Commit lag
        ImGui::TableSetColumnIndex(4);

        if (auto commitLag = record.commitLag()) {
          ImGui::Text("%5.4f", *commitLag);
        } else {
          ImGui::Text("-");
        }

        // Gen time
        ImGui::TableSetColumnIndex(5);

        if (record.generationCompleteTime >= 0.0) {
          ImGui::TextUnformatted(record.generationTimeUsText().c_str());
        } else {
          ImGui::Text("-");
        }

        // Input Primitives
        ImGui::TableSetColumnIndex(6);

        if (record.generationCompleteTime >= 0.0) {
          ImGui::Text("%d", record.stats.prim.candidateCount);
        } else {
          ImGui::Text("-");
        }

        // Visible Primitives
        ImGui::TableSetColumnIndex(7);

        if (record.generationCompleteTime >= 0.0) {
          ImGui::Text("%d", record.stats.prim.visibleCount);
        } else {
          ImGui::Text("-");
        }

        // Visible Primitives
        ImGui::TableSetColumnIndex(8);

        if (record.generationCompleteTime >= 0.0) {
          ImGui::Text("%d", record.stats.prim.updateVertexCount);
        } else {
          ImGui::Text("-");
        }

        auto const& arrangement = record.stats.arrangement;
        auto showArrangementStat = [&](int column, uint32_t value) {
          ImGui::TableSetColumnIndex(column);
          if (record.generationCompleteTime >= 0.0) {
            ImGui::Text("%u", value);
          } else {
            ImGui::Text("-");
          }
        };
        auto showArrangementTime = [&](int column, uint64_t nanoseconds) {
          ImGui::TableSetColumnIndex(column);
          if (record.generationCompleteTime >= 0.0) {
            ImGui::Text("%llu", static_cast<unsigned long long>(nanoseconds / 1000));
          } else {
            ImGui::Text("-");
          }
        };

        showArrangementStat(9, arrangement.vertexCount);
        showArrangementStat(10, arrangement.edgeCount);
        showArrangementStat(11, arrangement.faceCount);
        showArrangementStat(12, arrangement.triangleCount);
        showArrangementStat(13, arrangement.wallCount);
        showArrangementStat(14, arrangement.chipCount);
        showArrangementStat(15, arrangement.wedgeCount);
        showArrangementTime(16, arrangement.buildPSLGTimeNs);
        showArrangementTime(17, arrangement.classificationTimeNs);
      }

      ImGui::EndTable();
    }
  }

  ImGui::End();
}

void StatePlayBooleanWorld::debug_renderOptions() {
  if (!mDebugDisplay.options) {
    return;
  }

  // The entity handler holds the options the player is actually turned with,
  // so edit those rather than a copy the launcher's configuration would win
  // back on the next map load.
  auto entityHandler = static_pointer_cast<EntityHandlerBooleanWorld>(
      applib::ModelInstance::entityHandler());

  auto inputOptions = entityHandler->getInputOptions();

  if (ImGui::Begin("Options")) {
    ImGui::TextUnformatted("Input");
    if (ImGui::SliderFloat(
            "Mouse sensitivity",
            &inputOptions.mouseSensitivity,
            gImGui_MouseSensitivityMin,
            gImGui_MouseSensitivityMax,
            "%.2f")) {
      entityHandler->setInputOptions(inputOptions);
    }

    ImGui::TextDisabled("Not saved - set Input/MouseSensitivity to keep a value.");

    ImGui::Separator();
    ImGui::TextUnformatted("World (F5 session-only)");
    ImGui::SliderFloat(
        "Pixel size", &mDebugDisplay.pixelSize, 1.0f / 32.0f, 1.0f,
        "%.5f");
    if (ImGui::SliderInt(
            "Wedge quality", &mDebugDisplay.wedgeQuality, 0, 3)) {
      auto world = getMap()->getWorld();
      auto wedgeSettings = world->getWedgeGenerationParameters();
      wedgeSettings.quality = uint32_t(mDebugDisplay.wedgeQuality);
      world->setWedgeGenerationParameters(wedgeSettings);
      if (auto generator = getWDG()) {
        generator->generate();
      }
    }
    ImGui::TextDisabled(
        "Regenerates Wedges for this play session; does not save the World.");

    {
      auto opacityOverrideEnabled =
          mDebugDisplay.liquidOpacityOverride.has_value();
      if (ImGui::Checkbox(
              "Override liquid opacity", &opacityOverrideEnabled)) {
        mDebugDisplay.liquidOpacityOverride = opacityOverrideEnabled
            ? std::optional<float>{getLiquidPropertiesAt(
                                        getPlayerPosition())
                                        .opacity}
            : std::nullopt;
      }
      ImGui::BeginDisabled(!opacityOverrideEnabled);
      auto opacity = mDebugDisplay.liquidOpacityOverride.value_or(0.0f);
      if (ImGui::SliderFloat(
              "Opacity##Liquid", &opacity, 0.0f, 0.999f, "%.3f")) {
        mDebugDisplay.liquidOpacityOverride = opacity;
      }
      ImGui::EndDisabled();

      auto tintOverrideEnabled =
          mDebugDisplay.liquidTintOverride.has_value();
      if (ImGui::Checkbox("Override liquid colour", &tintOverrideEnabled)) {
        mDebugDisplay.liquidTintOverride = tintOverrideEnabled
            ? std::optional<std::array<float, 3>>{getLiquidPropertiesAt(
                                                        getPlayerPosition())
                                                        .tint}
            : std::nullopt;
      }
      ImGui::BeginDisabled(!tintOverrideEnabled);
      auto tint = mDebugDisplay.liquidTintOverride.value_or(
          std::array<float, 3>{0.5f, 0.5f, 0.5f});
      if (ImGui::ColorEdit3("Colour##Liquid", tint.data())) {
        mDebugDisplay.liquidTintOverride = tint;
      }
      ImGui::EndDisabled();

      auto reflectanceOverrideEnabled =
          mDebugDisplay.liquidReflectanceOverride.has_value();
      if (ImGui::Checkbox(
              "Override liquid reflectance", &reflectanceOverrideEnabled)) {
        mDebugDisplay.liquidReflectanceOverride = reflectanceOverrideEnabled
            ? std::optional<float>{getLiquidPropertiesAt(
                                        getPlayerPosition())
                                        .reflectance}
            : std::nullopt;
      }
      ImGui::BeginDisabled(!reflectanceOverrideEnabled);
      auto reflectance =
          mDebugDisplay.liquidReflectanceOverride.value_or(0.0f);
      if (ImGui::SliderFloat(
              "Reflectance##Liquid", &reflectance, 0.0f, 1.0f, "%.3f")) {
        mDebugDisplay.liquidReflectanceOverride = reflectance;
      }
      ImGui::EndDisabled();

      auto f0OverrideEnabled = mDebugDisplay.liquidF0Override.has_value();
      if (ImGui::Checkbox("Override liquid F0", &f0OverrideEnabled)) {
        mDebugDisplay.liquidF0Override = f0OverrideEnabled
            ? std::optional<float>{getLiquidPropertiesAt(
                                        getPlayerPosition())
                                        .f0}
            : std::nullopt;
      }
      ImGui::BeginDisabled(!f0OverrideEnabled);
      auto f0 = mDebugDisplay.liquidF0Override.value_or(0.0f);
      if (ImGui::SliderFloat("F0##Liquid", &f0, 0.0f, 1.0f, "%.3f")) {
        mDebugDisplay.liquidF0Override = f0;
      }
      ImGui::EndDisabled();

      auto model =
          static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
      ImGui::Checkbox(
          "Enable water reflections",
          &mDebugDisplay.liquidReflectionEnabled);

      auto activeTechnique = model->getWaterReflectionTechnique();
      if (ImGui::BeginCombo(
              "Water reflection technique",
              bw::app::waterReflectionTechniqueName(activeTechnique).data())) {
        for (auto technique : bw::app::allWaterReflectionTechniques) {
          auto selected = technique == activeTechnique;
          if (ImGui::Selectable(
                  bw::app::waterReflectionTechniqueName(technique).data(),
                  selected)) {
            model->setWaterReflectionTechnique(technique);
            activeTechnique = technique;
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::BeginDisabled(
          !mDebugDisplay.liquidReflectionEnabled ||
          activeTechnique != bw::app::WaterReflectionTechnique::Planar);
      auto activePlanarResolution = model->getPlanarReflectionResolution();
      if (ImGui::BeginCombo(
              "Planar reflection resolution",
              bw::app::planarReflectionResolutionName(
                  activePlanarResolution).data())) {
        for (auto resolution : bw::app::allPlanarReflectionResolutions) {
          auto selected = resolution == activePlanarResolution;
          if (ImGui::Selectable(
                  bw::app::planarReflectionResolutionName(resolution).data(),
                  selected)) {
            model->setPlanarReflectionResolution(resolution);
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();

      if (activeTechnique ==
          bw::app::WaterReflectionTechnique::ScreenSpace) {
        ImGui::BeginDisabled(!mDebugDisplay.liquidReflectionEnabled);
        ImGui::SliderFloat(
            "Reflection mip level##Liquid",
            &mDebugDisplay.liquidReflectionMipLevel, 0.0f, 4.0f, "%.2f");
        ImGui::EndDisabled();
      }
      ImGui::TextDisabled(
          "Debug-only - optical overrides and reflection controls are not "
          "saved to Game.yaml.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Video");

    // The model owns the session's active scale, rather than the map-owned
    // renderer or launch configuration, so a selection is ready for the next
    // frame and survives every map transition.
    auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
    auto activeRenderScale = model->getActiveRenderScale();
    if (ImGui::BeginCombo(
            "Render scale",
            bw::app::renderScaleName(activeRenderScale).data())) {
      for (auto renderScale : bw::app::allRenderScales) {
        auto selected = renderScale == activeRenderScale;
        if (ImGui::Selectable(
                bw::app::renderScaleName(renderScale).data(), selected)) {
          model->setActiveRenderScale(renderScale);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::TextDisabled("Not saved - set Video/RenderScale to keep a value.");

    auto activeAntiAliasing = model->getActiveAntiAliasing();
    if (ImGui::BeginCombo(
            "Anti-aliasing",
            bw::app::antiAliasingLabel(activeAntiAliasing).data())) {
      for (auto antiAliasing : bw::app::allAntiAliasingOptions) {
        auto samples = bw::app::antiAliasingMsaaSamples(antiAliasing);
        auto supported = mwRenderSystem->getCaps().supportsMsaa(samples);
        auto selected = antiAliasing == activeAntiAliasing;
        auto flags = supported ? ImGuiSelectableFlags_None
                               : ImGuiSelectableFlags_Disabled;
        if (ImGui::Selectable(
                bw::app::antiAliasingLabel(antiAliasing).data(),
                selected, flags)) {
          model->setActiveAntiAliasing(antiAliasing);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::TextDisabled("Not saved - set Video/AA to keep a value.");

    ImGui::Checkbox("Wireframe world", &mDebugDisplay.wireframe);
    ImGui::TextDisabled("Debug-only - renders world surfaces as polygon lines.");
    ImGui::Checkbox("Depth pre-pass", &mDebugDisplay.depthPrepass);
    ImGui::TextDisabled(
        "Debug-only - fills depth first, then shades with a less-equal depth test.");
    ImGui::Checkbox(
        "Sort geometry closest first",
        &mDebugDisplay.sortGeometryFrontToBack);
    ImGui::TextDisabled(
        "Debug-only - sorts 3D draws and world triangles by view distance.");
    ImGui::Checkbox(
        "Visualize fragment overdraw",
        &mDebugDisplay.fragmentOverdraw);
    ImGui::TextDisabled(
        "Debug-only - accumulates world fragments without lighting or post-processing.");

    ImGui::Separator();
    auto configuredAmbientOcclusion = mDebugDisplay.ambientOcclusion;
    auto ambientOcclusionConfigured =
        configuredAmbientOcclusion != bw::app::AmbientOcclusion::None;
    auto gtaoConfigured =
        configuredAmbientOcclusion == bw::app::AmbientOcclusion::GtaoDepth ||
        configuredAmbientOcclusion == bw::app::AmbientOcclusion::GtaoNormals;
    auto ambientOcclusionLabel = gtaoConfigured
                                     ? "Enable GTAO"
                                 : configuredAmbientOcclusion ==
                                         bw::app::AmbientOcclusion::Ssao
                                     ? "Enable SSAO"
                                     : "Enable ambient occlusion";
    ImGui::TextUnformatted(
        configuredAmbientOcclusion == bw::app::AmbientOcclusion::GtaoDepth
            ? "Ambient occlusion (GTAO, depth normals)"
        : configuredAmbientOcclusion == bw::app::AmbientOcclusion::GtaoNormals
            ? "Ambient occlusion (GTAO, MRT normals)"
        : configuredAmbientOcclusion == bw::app::AmbientOcclusion::Ssao
            ? "Ambient occlusion (SSAO)"
            : "Ambient occlusion");
    ImGui::BeginDisabled(!ambientOcclusionConfigured);
    if (ImGui::Checkbox(
            ambientOcclusionLabel,
            &mDebugDisplay.ambientOcclusionEnabled)) {
      // Toggling AO changes the generated graph topology and its named FXAA
      // output. Evict every variant so it is recreated with matching options.
      for (auto& [key, pipeline] : mWorldRenderPipelines) {
        if (pipeline) {
          mwRenderSystem->removeRenderPipeline(pipeline->getName());
          pipeline.reset();
        }
      }
      mWorldRenderPipelines.clear();
    }

    bool ambientOcclusionChanged = false;
    ImGui::BeginDisabled(!mDebugDisplay.ambientOcclusionEnabled);
    if (configuredAmbientOcclusion == bw::app::AmbientOcclusion::Ssao) {
      auto& ssao = mDebugDisplay.ssao;
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Radius##SSAO", &ssao.radius, 0.0f, 10.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Intensity##SSAO", &ssao.intensity, 0.0f, 5.0f, "%.2f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Bias##SSAO", &ssao.bias, 0.0f, 1.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Power##SSAO", &ssao.power, 0.1f, 8.0f, "%.2f");
      ambientOcclusionChanged |= ImGui::SliderInt(
          "Sample count##SSAO", &ssao.sampleCount, 1, 64);
      ambientOcclusionChanged |= ImGui::SliderInt(
          "Blur radius##SSAO", &ssao.blurRadius, 0, 8);
    } else if (gtaoConfigured) {
      auto& gtao = mDebugDisplay.gtao;
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Radius##GTAO", &gtao.radius, 0.0f, 10.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Intensity##GTAO", &gtao.intensity, 0.0f, 5.0f, "%.2f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Thickness##GTAO", &gtao.thickness, 0.0f, 5.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Horizon bias##GTAO", &gtao.horizonBias, 0.0f, 1.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Falloff start##GTAO", &gtao.falloffStart, 0.0f,
          gtao.falloffEnd, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Falloff end##GTAO", &gtao.falloffEnd,
          gtao.falloffStart, 1.0f, "%.3f");
      ambientOcclusionChanged |= ImGui::SliderInt(
          "Slice count##GTAO", &gtao.sliceCount, 1, 16);
      ambientOcclusionChanged |= ImGui::SliderInt(
          "Steps per slice##GTAO", &gtao.stepsPerSlice, 1, 16);
      ambientOcclusionChanged |= ImGui::SliderFloat(
          "Power##GTAO", &gtao.power, 0.1f, 8.0f, "%.2f");
      ambientOcclusionChanged |= ImGui::SliderInt(
          "Blur radius##GTAO", &gtao.blurRadius, 0, 8);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (ambientOcclusionChanged) {
      mpp::AmbientOcclusionOptions ambientOcclusion;
      ambientOcclusion.method = gtaoConfigured
                                    ? mpp::AmbientOcclusionMethod::Gtao
                                    : mpp::AmbientOcclusionMethod::Ssao;
      ambientOcclusion.ssao = mDebugDisplay.ssao;
      ambientOcclusion.gtao = mDebugDisplay.gtao;
      // setAmbientOcclusionOptions replaces the pipeline's whole options
      // struct, so the liquid modulation input has to be restated here or
      // dragging any tuning slider would silently drop it and put ambient
      // occlusion back to full strength under liquid. These pipelines were
      // built with ambient occlusion on, so their extra outputs are declared.
      ambientOcclusion.modulationInput = gLiquidRetentionImageName;
      for (auto const& [key, pipeline] : mWorldRenderPipelines) {
        if (pipeline) {
          pipeline->setAmbientOcclusionOptions(ambientOcclusion);
        }
      }
    }
    ImGui::TextDisabled(
        ambientOcclusionConfigured
            ? "Debug-only - set Video/AmbientOcclusion to choose the method."
            : "Disabled by Video/AmbientOcclusion: none.");

    ImGui::Separator();
    ImGui::TextUnformatted("Player Torch (F5 session-only)");
    ImGui::SliderFloat(
        "Distance ahead of player##PlayerTorch", &mDebugDisplay.lightDistance,
        0.0f, 256.0f, "%.1f");
    ImGui::TextDisabled(
        "Moves the Torch from the player's eye along the current facing direction.");
    ImGui::SliderFloat(
        "Attenuation radius##PlayerTorch",
        &mDebugDisplay.playerTorch.attenuationRadius,
        0.01f, 1024.0f, "%.2f");
    mDebugDisplay.playerTorch.attenuationFalloff = min(
        mDebugDisplay.playerTorch.attenuationFalloff,
        mDebugDisplay.playerTorch.attenuationRadius);
    ImGui::SliderFloat(
        "Falloff width##PlayerTorch",
        &mDebugDisplay.playerTorch.attenuationFalloff,
        0.0f, mDebugDisplay.playerTorch.attenuationRadius, "%.2f");
    ImGui::TextDisabled(
        "Not saved - set Video/PlayerTorch to keep attenuation values.");
    auto& sessionShadows = mDebugDisplay.playerTorchShadows;
    auto const& configuredShadows = model->getShadowOptions();
    char const* enableLabels[] = {
        "Configured", "Force enabled", "Force disabled"};
    auto enableSelection = sessionShadows.enabledOverride
                               ? (*sessionShadows.enabledOverride ? 1 : 2)
                               : 0;
    if (ImGui::Combo("Enable override", &enableSelection, enableLabels, 3)) {
      sessionShadows.enabledOverride = enableSelection == 0
                                           ? std::nullopt
                                           : std::optional<bool>{enableSelection == 1};
    }
    ImGui::Text("Cubemap resolution (configured): %zu", configuredShadows.faceResolution);
    ImGui::TextDisabled("Resolution is read-only during play; no live cubemap reallocation.");
    ImGui::SliderFloat(
        "Range##PlayerTorch", &sessionShadows.options.range,
        sessionShadows.options.nearPlane + 0.01f,
        max(1024.0f, sessionShadows.options.nearPlane + 0.01f), "%.2f");
    ImGui::SliderFloat("Constant bias##PlayerTorch",
                       &sessionShadows.options.constantBias, 0.0f, 0.02f, "%.5f");
    ImGui::SliderFloat("Normal bias##PlayerTorch",
                       &sessionShadows.options.normalBias, 0.0f, 0.02f, "%.5f");
    auto filterSelection =
        sessionShadows.options.filter == bw::app::ShadowFilter::Hard ? 0 : 1;
    char const* filterLabels[] = {"Hard", "PCF 3x3"};
    if (ImGui::Combo("Filter##PlayerTorch", &filterSelection, filterLabels, 2)) {
      sessionShadows.options.filter = filterSelection == 0
                                          ? bw::app::ShadowFilter::Hard
                                          : bw::app::ShadowFilter::Pcf;
    }
    ImGui::SliderFloat("PCF radius##PlayerTorch",
                       &sessionShadows.options.filterRadius, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("Fade start##PlayerTorch",
                       &sessionShadows.options.fadeStart, 0.0f, 1.0f, "%.3f");
    if (mPlayerTorchShadowHardwareFallback) {
      ImGui::TextDisabled("Unavailable after hardware/allocation fallback.");
    } else {
      ImGui::TextDisabled("Not saved - set Video/Shadows to keep these values.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Vignette");
    ImGui::ColorEdit3(
        "Colour", mDebugDisplay.vignetteColour.data());
    ImGui::SliderFloat(
        "Strength", &mDebugDisplay.vignetteStrength,
        0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(
        "Inner radius", &mDebugDisplay.vignetteInnerRadius,
        0.0f, 1.4f, "%.2f");
    ImGui::SliderFloat(
        "Falloff width", &mDebugDisplay.vignetteFalloffWidth,
        0.01f, 1.4f, "%.2f");
  }

  ImGui::End();
}

void StatePlayBooleanWorld::_renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData) {
  VAR_UNUSED(frameTime);

  ImGuiDllBoundaryState imGuiBoundaryState{
      static_cast<ImGuiContext*>(imGuiCtx),
      static_cast<ImPlotContext*>(imPlotCtx),
      static_cast<ImGuiMemAllocFunc>(allocFunc),
      static_cast<ImGuiMemFreeFunc>(freeFunc),
      userData};

  //
  // Render
  //
  ImGuiIO& io = ImGui::GetIO();
  wp::Vector2 viewSize{(float)mwRenderSystem->getWindowWidth(), (float)mwRenderSystem->getWindowHeight()};
  wp::Vector2 viewOffset = getPlayerPosition() - viewSize * 0.5f;
  wp::Vector2 viewScale{1.0f, 1.0f};
  wp::BoundingBox viewBounds{viewOffset, {io.DisplaySize.x, io.DisplaySize.y}};

  auto drawList = ImGui::GetBackgroundDrawList();

  debug_renderMinimap(viewSize, viewOffset, viewScale, viewBounds, drawList);
  debug_renderCollisionSim(viewSize, viewOffset, viewScale, drawList);
  debug_renderClipGenerationInfo(drawList);
  debug_renderOptions();
}
