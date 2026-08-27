#pragma once

#include <array>
#include <map>
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
#include "VideoOptions.h"
#include "WorldTriangle3dDataProvider.h"
#include "WorldRenderer3d.h"

class WorldRenderer {
public:
  using RenderTargets = std::array<mpp::RenderTargetPtr, bw::app::renderScaleCount>;

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

  std::vector<MaterialRenderer> mMaterialRenderers;

  bool mWorldHasChanged;
  int32_t mHighlightedTriangle{-1};
  bool mHighlightedCeiling{};

  wp::Logger* mwLogger;

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
      bool highlightedCeiling);

  // Wall quad geometry. Each wall picks, every call, whichever single quad
  // currently faces the player: its authored material if the player is on
  // the side its normal points toward, or the reserved plain-white
  // material otherwise. Called every frame regardless of mWorldHasChanged,
  // since the player moving is enough to flip that choice for a wall even
  // when nothing about the world itself changed.
  void updateWallDataProvider(
      bw::core::WorldData const& worldData, glm::vec3 const& playerPosition,
      int32_t highlightedWall);

  uint32_t addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c);

  // Emits one Chip detail triangle, mapping it out of arrangement space
  // (Z up) into renderer space. `mirrored` flips it for a wall drawn from
  // behind, exactly as the wall quad itself is flipped there.
  void addDetailTriangleToDataProvider(
      DataProvider dataProvider,
      uint32_t meshIndex,
      bw::core::arr::DetailTriangle const& triangle,
      bool mirrored,
      uint32_t colour);

public:
  WorldRenderer(
      wp::application::resourcesystem::ResourceManager* resourceMgr,
      wp::Logger* logger,
      bw::app::RenderTextureFilter renderTextureFilter,
      bw::app::HorizontalMaterials horizontalMaterials);

  virtual ~WorldRenderer();

  void setWorldChanged();

  // Updates every existing floor, ceiling, and wall mesh bucket that was
  // baked for this Sub-material. Used by the editor's unsaved draft; it does
  // not rebuild arrangement geometry or batches.
  void updateSubMaterialDraft(
      std::string const& subMaterialId, int32_t materialIndex,
      bw::core::MaterialDefinitionData const& definition);

  // Rebuilds the read-only render-side Sub-material cache after the editor
  // saves ProcMaterial YAML directly to disk.
  void reloadSubMaterialResolver(
      wp::application::resourcesystem::ResourceManager* resourceMgr);

  void create(mpp::ScenePtr scene, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  // Builds all offscreen targets the world can be composited from. This talks
  // to OpenGL, so it belongs on the main thread - the map load post-work step.
  void createRenderTargets(mpp::RenderSystem* renderSystem);

  mpp::RenderTargetPtr const& getRenderTarget(bw::app::RenderScale renderScale) const;

  // Hands the targets to a caller that outlives this renderer, so map unload
  // can release them from its main-thread post-work step rather than from the
  // threadable pre-work that tears the renderer itself down.
  RenderTargets detachRenderTargets();

  void update(
      bw::core::World* world,
      bw::core::WorldData const& worldData,
      glm::vec3 const& playerPosition,
      glm::vec3 const& lightPosition,
      int32_t horizontalMaterialIndexOverride,
      int32_t wallMaterialIndexOverride,
      float materialScale,
      float farGridSize,
      SecondaryMaterialOptions const& secondaryMaterial,
      float frameTime,
      int32_t highlightedTriangle = -1,
      bool highlightedCeiling = false,
      int32_t highlightedWall = -1);
};
