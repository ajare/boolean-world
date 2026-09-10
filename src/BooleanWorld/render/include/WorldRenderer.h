#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>
#include <string>

#include <glm/vec3.hpp>

#include <mpp/Scene.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/World.h>

#include "SecondaryMaterialOptions.h"
#include "SubMaterialResolver.h"
#include "SurfaceMaterialResolver.h"
#include "VideoOptions.h"
#include "WorldTriangle3dDataProvider.h"
#include "WorldRenderer3d.h"
#include "WallRenderVariant.h"

class WorldRenderer {
public:
  enum class WallUpdatePolicy {
    GameplayViewerSideChanges,
    EditorEveryUpdate,
  };

  class PreparedWorldRenderData;
  using PreparedWorldRenderDataPtr =
      std::shared_ptr<PreparedWorldRenderData>;

  using RenderTargets = std::array<mpp::RenderTargetPtr, bw::app::renderScaleCount>;
  using WallRenderVariantResolver = std::function<std::optional<WallRenderVariant>(
      bw::core::arr::ArrangementWall const&)>;

private:
  typedef std::shared_ptr<WorldTriangle3dDataProvider> DataProvider;

  typedef std::shared_ptr<WorldRenderer3d> Renderer;

  struct MaterialRenderer {
    Renderer renderer;
    DataProvider dataProvider;
    WorldSurfaceSet surfaceSet;
  };

private:
  // Current render-side ProcMaterial data, refreshed after an editor save.
  SubMaterialResolver mSubMaterialResolver;
  // The definitions that named the already-created mesh buckets. Geometry
  // remains in these buckets until the preview closes, even after a save
  // changes a definition's hash.
  SubMaterialResolver mBakedSubMaterialResolver;
  SurfaceMaterialResolver mSurfaceMaterialResolver;
  SurfaceMaterialResolver mBakedSurfaceMaterialResolver;

  wp::application::resourcesystem::ResourceManager* mResourceMgr{};
  std::string mWorldResourceNamespace;
  std::string mBatchNamePrefix;
  std::map<std::string, WallRenderVariant> mWallImageVariants;
  std::vector<MaterialRenderer> mMaterialRenderers;
  std::vector<WallRenderSurface> mWallRenderSurfaces;
  WallRenderVariantResolver mWallRenderVariantResolver;
  WallUpdatePolicy mWallUpdatePolicy;

  bool mWorldHasChanged;
  bool mWireframe{false};
  bool mFragmentOverdraw{false};
  int32_t mHighlightedTriangle{-1};
  bool mHighlightedCeiling{};
  std::vector<uint8_t> mWallFacingNormalSides;

  wp::Logger* mwLogger;
  bw::core::World* mwWorld{nullptr};

  bw::app::RenderTextureFilter mRenderTextureFilter;

  // All scale targets are ready for the map's lifetime (ADR 0012), so changing
  // the model's active scale only chooses another target.
  RenderTargets mWorldTargets;

private:
  // Floor/ceiling triangle geometry - unaffected by player position, so
  // this stays gated by mWorldHasChanged like before.
  void updateHorizontalDataProvider(
      bw::core::WorldData const& worldData,
      int32_t highlightedTriangle,
      bool highlightedCeiling,
      DataProvider const& dataProvider);

  // Liquid has its own blended scene model so it can be deferred without
  // changing the opaque floor and ceiling model.
  void updateLiquidDataProvider(
      bw::core::WorldData const& worldData,
      DataProvider const& dataProvider);

  // Wall surface geometry. Each wall picks whichever single triangular or
  // quadrilateral side faces the supplied viewer position: its authored
  // material on the side its normal points toward, or the reserved plain-white
  // material otherwise. Gameplay rebuilds only when one of those side choices
  // changes; the editor can opt into rebuilding on every preview update.
  [[nodiscard]] std::vector<uint8_t> wallFacingNormalSides(
      bw::core::WorldData const& worldData,
      glm::vec3 const& viewerPosition) const;

  void updateWallDataProvider(
      bw::core::WorldData const& worldData,
      std::vector<uint8_t> const& facingNormalSides,
      int32_t highlightedWall, DataProvider const& dataProvider);

  uint32_t addVertexToDataProvider(
      DataProvider dataProvider, uint32_t meshIndex, float px, float py,
      float pz, float nx, float ny, float nz, float u, float v, uint32_t c,
      float liquidSurfaceHeight =
          WorldTriangle3dDataProvider::dryLiquidSurfaceHeight,
      float surfaceUpX = 0.0f, float surfaceUpY = 1.0f,
      float surfaceUpZ = 0.0f,
      std::optional<std::array<float, 3>> const& projectionNormal =
          std::nullopt);

  // Emits one Chip detail triangle, mapping it out of arrangement space
  // (Z up) into renderer space. `mirrored` flips it for a wall drawn from
  // behind, exactly as the wall surface itself is flipped there.
  void addDetailTriangleToDataProvider(
      DataProvider dataProvider,
      uint32_t meshIndex,
      bw::core::arr::DetailTriangle const& triangle,
      bool mirrored,
      uint32_t colour,
      float liquidSurfaceHeight =
          WorldTriangle3dDataProvider::dryLiquidSurfaceHeight,
      float surfaceUpX = 0.0f, float surfaceUpY = 1.0f,
      float surfaceUpZ = 0.0f);

public:
  WorldRenderer(
      wp::application::resourcesystem::ResourceManager* resourceMgr,
      wp::Logger* logger,
      bw::app::RenderTextureFilter renderTextureFilter,
      bw::app::HorizontalMaterials horizontalMaterials,
      WallUpdatePolicy wallUpdatePolicy,
      std::vector<WallRenderSurface> wallRenderSurfaces = {},
      WallRenderVariantResolver wallRenderVariantResolver = {},
      std::string worldResourceNamespace = "World",
      bool deferLiquidToWaterPass = false,
      std::string batchNamePrefix = "World3d");

  virtual ~WorldRenderer();

  void setWorldChanged();

  // The current geometry count for one independently submitted surface set.
  // This is useful to renderer integrations that need to inspect a snapshot
  // without conflating opaque and blended scene models.
  [[nodiscard]] uint32_t getSurfaceTriangleCount(WorldSurfaceSet surfaceSet) const;

  // Toggles line polygon mode for the world's horizontal and wall meshes.
  void setWireframe(bool wireframe);

  // Replaces normal world materials with the low-cost accumulating fragment
  // overdraw shader. This affects world geometry only.
  void setFragmentOverdraw(bool enabled);

  // Updates every existing floor, ceiling, and wall mesh bucket that was
  // baked for this Sub-material. Used by the editor's unsaved draft; it does
  // not rebuild arrangement geometry or batches.
  void updateSubMaterialDraft(
      std::string const& subMaterialId, int32_t materialIndex,
      bw::core::MaterialDefinitionData const& definition);

  // Applies an unsaved global-preset draft to every existing bucket using
  // that preset while preserving each bucket's Sub-material definition.
  void updateEmbossPresetDraft(
      std::string const& embossPresetId,
      bw::core::EmbossData const& emboss);

  // Rebuilds the read-only render-side Sub-material cache after the editor
  // saves ProcMaterial YAML directly to disk.
  void reloadSubMaterialResolver(
      wp::application::resourcesystem::ResourceManager* resourceMgr);

  void create(mpp::ScenePtr scene, bw::core::World* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  // Builds all offscreen targets the world can be composited from. This talks
  // to OpenGL, so it belongs on the main thread - the map load post-work step.
  void createRenderTargets(mpp::RenderSystem* renderSystem);

  mpp::RenderTargetPtr const& getRenderTarget(bw::app::RenderScale renderScale) const;

  // Hands the targets to a caller that outlives this renderer, so map unload
  // can release them from its main-thread post-work step rather than from the
  // threadable pre-work that tears the renderer itself down.
  RenderTargets detachRenderTargets();

  // Builds immutable CPU-side mesh payloads without touching active providers
  // or OpenGL. Gameplay calls this from the world-generation worker.
  [[nodiscard]] PreparedWorldRenderDataPtr prepareWorldRenderData(
      bw::core::WorldDataPtr worldData,
      glm::vec3 const& viewerPosition);

  // Replaces the active providers' payloads while preserving the provider
  // identities held by MPP. Must be called on the main/render thread.
  void publishWorldRenderData(
      PreparedWorldRenderDataPtr const& prepared);

  void update(
      bw::core::World* world,
      bw::core::WorldData const& worldData,
      glm::vec3 const& playerPosition,
      glm::vec3 const& lightPosition,
      bw::app::PlayerTorchOptions const& playerTorch,
      std::optional<float> liquidOpacityOverride,
      std::optional<std::array<float, 3>> const& liquidTintOverride,
      std::optional<float> liquidReflectanceOverride,
      std::optional<float> liquidF0Override,
      float liquidReflectionMipLevel,
      bool liquidReflectionEnabled,
      bool sortGeometryFrontToBack,
      int32_t horizontalMaterialIndexOverride,
      int32_t wallMaterialIndexOverride,
      float materialScale,
      float pixelSize,
      SecondaryMaterialOptions const& secondaryMaterial,
      float frameTime,
      int32_t highlightedTriangle = -1,
      bool highlightedCeiling = false,
      int32_t highlightedWall = -1);
};
