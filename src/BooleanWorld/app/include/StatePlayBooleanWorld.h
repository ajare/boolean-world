#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <mpp/AmbientOcclusion.h>
#include <mpp/AntiAliasing.h>
#include <mpp/Camera.h>
#include <mpp/SceneModel3d.h>

#include <willpower/application/StateFactory.h>

#include <willpower/collide/Collider.h>

#include <willpower/common/AccelerationGrid.h>

#include <willpower/viz/DynamicTriangleRenderer.h>

#include <applib/StatePlay.h>

#include <core/ArrangementWorldData.h>
#include <core/LiquidProperties.h>
#include <core/DynamicWorldDataGenerator.h>

#include "imgui/imgui.h"

#include "Platform.h"
#include "WorldCollisionSim.h"
#include "WorldRenderer.h"
#include "Map.h"
#include "DisplayMessage.h"
#include "ClippingRecord.h"
#include "PlayerTorchShadows.h"
#include "PlayerWallDepenetration.h"
#include "LiquidReflectionSelection.h"
#include "SteamAudio.h"
#include "VideoOptions.h"

namespace mpp {
struct GraphImageCapture;
struct GraphPassExecutionStats;
}

namespace FMOD {
namespace Studio {
class EventInstance;
}  // namespace Studio
}  // namespace FMOD

class APPLICATION_API StatePlayBooleanWorld : public applib::StatePlay {
  struct DebugDisplay {
    bool minimap{false};
    bool collisionSim{false};
    bool clipGeneration{false};
    bool options{false};
    bool audio{false};
    bool wireframe{false};
    bool depthPrepass{true};
    bool sortGeometryFrontToBack{false};
    bool fragmentOverdraw{false};
    int wedgeQuality{0};
    float pixelSize{1.0f / 32.0f};

    bw::app::AmbientOcclusion ambientOcclusion{
        bw::app::AmbientOcclusion::GtaoDepth};
    bool ambientOcclusionEnabled{true};
    mpp::SSAOOptions ssao;
    mpp::GTAOOptions gtao;

    std::array<float, 3> vignetteColour{0.0f, 0.0f, 0.0f};
    float vignetteStrength{0.58f};
    float vignetteInnerRadius{0.55f};
    float vignetteFalloffWidth{0.65f};

    // F5 session-only optical overrides for Liquid at the player's position.
    // Unset property overrides leave core/LiquidProperties.h untouched; the
    // Reflection mip level controls only Screen-space sampling blur.
    std::optional<float> liquidOpacityOverride;
    std::optional<std::array<float, 3>> liquidTintOverride;
    std::optional<float> liquidReflectanceOverride;
    std::optional<float> liquidF0Override;
    float liquidReflectionMipLevel{defaultLiquidReflectionMipLevel};
    bool liquidReflectionEnabled{true};

    float lightDistance{0.0f};
    bw::app::PlayerTorchOptions playerTorch;

    // A live diagnostic copy; it is intentionally not the model's configured
    // VideoOptions and is discarded with this play session.
    bw::app::PlayerTorchShadowSessionOptions playerTorchShadows;

    bool _renderTriangulationLines{false};

    bool active() const {
      return minimap || collisionSim || clipGeneration || options || audio;
    }
  };

private:
  typedef wp::viz::DynamicTriangleRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat> WorldTriangleRenderer;

private:
  double mGlobalTime;

  mpp::CameraPtr mCamera3d;

  // Data-driven fullscreen programs declared by World/Resources.yaml.
  mpp::ResourcePtr mVignetteProgram;
  mpp::ResourcePtr mFragmentOverdrawResolveProgram;

  bw::core::WorldDataPtr mWorldData;

  WorldCollisionSim* mWorldCollisionSim;

  wp::collide::Collider* mPlayerCollider;

  // Debug toggle: fold across every Layer, or just the first one.
  bool mAllLayers;

  int32_t mPlayerPolygonIndex;

  int32_t mPlayerBorderIntersectIndex;

  uint32_t mCollisionsProcessed;

  float mPlayerPrevAngle, mPlayerPrevPitch;

  // Vertical physics (ticket: step-up/gravity). The PhysicalStats feet
  // elevation is the player's current simulated height; this is only its rate
  // of change while falling - stepping up is a direct, velocity-free rise.
  float mPlayerVerticalVelocity;

  // Captured before entity collision resolves this frame's horizontal move.
  // The post-entity pass traces the actual resolved chord through ordered
  // Arrangement faces to follow continuous floors and enforce affine
  // clearance between edges.
  wp::Vector2 mPlayerTraversalStartPosition{};
  float mPlayerTraversalStartFeetElevation{0.0f};
  float mPlayerTraversalStartVerticalVelocity{0.0f};
  bool mPlayerTraversalStartValid{false};

  // True once PhysicalStats::feetElevation has been snapped to the sampled
  // floor at least once. Until mWorldData exists (early in map load) the floor
  // query falls back to 0, so the first valid reading is a snap, not a fall.
  bool mPlayerVerticalHeightInitialized;

  // Set for the frame in which a rebuilt snapshot cannot contain the player's
  // current vertical cylinder. This routes the rebuild through the same
  // invalid-location branch used for a missing face or unresolved wall overlap.
  bool mPlayerRebuildNeedsLocationRecovery{false};

  // Captured from this frame's peekInput call (see getWorldInput) and
  // consumed by updatePlayerVerticalPhysics - how hard fly controls are asking
  // the player to swim up or down, in -1 to 1. Effort rather than speed: what
  // it achieves depends on the liquid being swum through. Zero whenever the
  // player isn't swimming.
  float mPlayerSwimEffort{0.0f};

  bool mExitScheduled;

  bool mScreenshotRequested{false};
  bool mRenderGraphCaptureRequested{false};

  // Once MPP has disabled a requested domain after unsupported hardware or a
  // failed allocation, a session override must not retry around that fallback.
  bool mPlayerTorchShadowHardwareFallback{false};
  bool mPlayerTorchShadowRequestedEnabled{false};

  // A world-space marker for an offset Player Torch. It never casts shadows
  // and is hidden while the light remains at the player's eye.
  mpp::ResourcePtr mPlayerTorchMarkerModel;
  mpp::SceneModel3dPtr mPlayerTorchMarker;
  glm::vec3 mPlayerTorchMarkerPosition{0.0f};
  bool mPlayerTorchMarkerPositionValid{false};

  // Created/managed in load states
  WorldRenderer* mwRenderer;

  // Generated graph topology depends on the F5 technique, Planar resolution,
  // and selected Liquid elevations as well as render scale, AA, and depth
  // pre-pass. Stable string keys retain only variants actually encountered.
  std::map<std::string, mpp::RenderPipelinePtr> mWorldRenderPipelines;

  // Camera-motion hysteresis belongs to the active World and Planar technique.
  // Neither selection nor viewer-side history crosses those boundaries.
  bw::app::LiquidReflectionSelectionPolicy mLiquidReflectionSelection;
  std::optional<bw::app::WaterReflectionTechnique>
      mLiquidReflectionSelectionTechnique;

  // Sticky for this play session. MPP falls back to WaterScene without
  // reflected radiance; gameplay keeps the configured technique as Planar.
  bool mPlanarReflectionSessionFailed{false};

  // Separate post-process-free pipelines retain the immutable enabled and
  // disabled depth-prepass modes. Each is resized to the selected world target
  // before use.
  std::array<mpp::RenderPipelinePtr, 2> mFragmentOverdrawPipelines;

  // ImGui view
  DebugDisplay mDebugDisplay;

  std::mutex mClippingRecordsMutex;

  std::deque<ClippingRecord> mClippingRecords;

  bw::core::DynamicWorldDataGenerator::GenerationCallbackToken mGenerationCallbackToken{
      bw::core::DynamicWorldDataGenerator::InvalidGenerationCallbackToken};
  std::atomic_bool mEmitterResyncPending{false};

  std::deque<DisplayMessage> mDisplayMessages;

  std::unique_ptr<AcousticPresetResolver> mAcousticPresetResolver;
  std::unique_ptr<bw::app::SteamAudio> mSteamAudio;

  // Phase 0 gate (#383): one theme event, started once and left running.
  FMOD::Studio::EventInstance* mThemeInstance{nullptr};

private:
  [[nodiscard]] bw::core::LayerSelection layerSelection() const;

  void createCamera();

  mpp::RenderPipelinePtr const& getOrCreateWorldRenderPipeline(
      bw::app::RenderScale renderScale,
      bw::app::AntiAliasing antiAliasing,
      std::vector<mpp::PlanarReflectionPlaneDescriptor> const& planarPlanes);

  mpp::RenderPipelinePtr const& getOrCreateFragmentOverdrawPipeline(
      bw::app::RenderScale renderScale);

  void setupMapRenderer(applib::StateTransitionData* transitionData) override;

  void createPlayerTorchMarker();

  void destroyPlayerTorchMarker();

  void updatePlayerTorchMarker(glm::vec3 const& lightPosition);

  std::map<std::string, std::tuple<wp::viz::Renderer*, int, bool>> createAdditionalRenderers(mpp::ResourceManager* renderResourceMgr) override;

  void registerInput() override;

  void createGameObjects(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

  void destroyGameObjects() override;

  void setupEntityFacades() override;

  void setupEntities() override;

  void updatePreInput(float frameTime) override;

  void updatePreEntities(float frameTime) override;

  void updatePostEntities(float frameTime) override;

  // Rises smoothly onto a taller floor and falls under gravity off a lower
  // one, at the player's current (post-collision) position. Run after
  // horizontal movement/collision has resolved that position, so the floor
  // queried here is the one the player actually ends up standing over.
  void updatePlayerVerticalPhysics(float frameTime);

  void updateAudio(float frameTime);

  Map* getMap();

  Map const* getMap() const;

  applib::PhysicalStats& getPlayerPhysicalStats();

  applib::PhysicalStats const& getPlayerPhysicalStats() const;

  wp::Vector2 getPlayerPosition() const;

  float getPlayerAngle() const;

  bool playerInWorld() const;

  bool playerIntersectsWorldBorders() const;

  void getWorldInput(wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* verticalEffort, float frameTime) const;

  void setupPlayerCollision();

  void createWorldCollisions(wp::Vector2 const& predictedPosition);

  void liftPlayerOffOverlappingWalls(
      std::span<bw::app::WallSegment const> walls);

  void exit();

  void handleClippingUpdate(bw::core::DynamicWorldDataGenerator::GenerationDetails const& details);

  bw::core::DynamicWorldDataGenerator* getWDG();

  uint32_t getPrimitiveAtPosition(wp::Vector2 const& pos) const;

  uint32_t getPlayerPrimitive() const;

  float getFloorElevationAt(wp::Vector2 const& pos) const;

  float getCeilingElevationAt(wp::Vector2 const& pos) const;

  float getPlayerFloorElevation() const;

  float getPlayerCeilingElevation() const;

  // How deep the player cylinder (base at PhysicalStats::feetElevation,
  // extending up BW_PLAYER_HEIGHT) is submerged below the liquid surface at
  // their position, clamped to [0, BW_PLAYER_HEIGHT]. Zero wherever there is no
  // liquid, or the player stands above its surface.
  float getPlayerLiquidSubmersionDepth() const;

  // Fraction of BW_PLAYER_SPEED available to the player given their current
  // submersion - see buoyantEquilibriumFraction. 1.0 wherever they are
  // dry.
  float getPlayerSwimSpeedMultiplier() const;

  // True where the liquid at the player's position is deep enough, measured
  // against the real floor beneath them, to require swimming rather than
  // wading - see BW_PLAYER_MIN_SWIM_SUBMERSION_FRACTION.
  bool isPlayerSwimming() const;

  // The properties of the liquid at pos - water's wherever there is none, so
  // callers that have already established there is liquid need not re-check.
  bw::core::LiquidProperties const& getLiquidPropertiesAt(
      wp::Vector2 const& pos) const;

  // The fraction of the player's height that sits below the surface when they
  // float at rest in the given liquid: their own density over the liquid's.
  // Above 1 in a liquid thinner than the player, who then sinks rather than
  // floating. Also the submersion at which the liquid has taken all of the
  // player's weight off the floor - see getPlayerSwimSpeedMultiplier.
  static float buoyantEquilibriumFraction(
      bw::core::LiquidProperties const& liquid);

  // Lifts the player out of the liquid onto an adjacent floor when they are
  // floating as high as swimming allows, pressed against the edge shared with
  // that floor, looking up over it, and its elevation is within
  // BW_PLAYER_MANTLE_WATER of their eye level with room to stand.
  // Moves them the shortest distance that puts them clear on the far side, or
  // leaves them where they are if no edge qualifies. Returns whether it moved
  // them.
  bool tryClimbOutOfLiquid();

  void addDisplayMessage(DisplayMessage::Level level, std::string const& message);

  void saveScreenshot(mpp::RenderSystem* renderSystem);
  void saveRenderGraphImages(
      std::vector<mpp::GraphImageCapture> const& captures,
      std::vector<mpp::GraphPassExecutionStats> const& passStats);

  void renderWorldThroughTarget(mpp::RenderSystem* renderSystem);

  // Debug / UI
  void debug_renderMinimap(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList);

  void debug_renderCollisionSim(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, ImDrawList* drawList);

  void debug_renderCpuUpdateTimings(
      double timelineEnd, double timelineDuration,
      float* graphStartX, float* graphEndX);

  void debug_renderClipGenerationInfo(ImDrawList* drawList);

  void debug_renderOptions();

  void debug_renderAudio();

  void ImGui_renderArrangement(bw::core::ArrangementWorldData const& worldData, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

  void ImGui_renderView(std::vector<wp::Vector2> const& viewVertices, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

  static ImVec2 wpVecToImVec2(wp::Vector2 const& v, wp::Vector2 const& offset, wp::Vector2 const& size, wp::Vector2 const& scale);

protected:
  void updateActions(std::vector<std::string> const& activeStates, float frameTime) override;

  void updatePreRenderers(float frameTime) override;

  void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

  void suspendImpl(void* args = nullptr) override;

  void resumeImpl(void* args) override;

  void updateImpl(float frameTime) override;

  void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

public:
  StatePlayBooleanWorld();

  ~StatePlayBooleanWorld();

  std::vector<std::string> getDebuggingText() const override;

  bool _imGuiActive() const override;

  bool _imGuiCapturesInput() const override;

  void _renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData) override;
};

class StatePlayBooleanWorldFactory : public wp::application::StateFactory {
  wp::Logger* mLogger;

public:
  explicit StatePlayBooleanWorldFactory(wp::Logger* logger)
      : wp::application::StateFactory("Play"), mLogger(logger) {
  }

  wp::application::State* createState() {
    auto state = new StatePlayBooleanWorld();
    state->setLogger(mLogger);
    return state;
  }
};
