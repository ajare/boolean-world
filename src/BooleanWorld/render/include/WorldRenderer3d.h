#pragma once

#include <memory>
#include <map>

#include <glm/vec3.hpp>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/UniformCollection.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/common/Logger.h>

#include "SecondaryMaterialOptions.h"
#include "VideoOptions.h"
#include "SubMaterialResolver.h"
#include "WorldBatchRenderer.h"
#include "WorldTriangle3dDataProvider.h"
#include "WallRenderVariant.h"

class WorldRenderer3d {
  wp::application::resourcesystem::ResourcePtr mMaterial;
  wp::application::resourcesystem::ResourcePtr mFragmentOverdrawMaterial;
  WorldSurfaceSet mSurfaceSet;

  SubMaterialResolver const* mwResolver;
  std::vector<WallRenderSurface> mWallRenderSurfaces;

  mpp::ScenePtr mScene;

  mpp::SceneModel3dPtr mSceneModel;

  std::vector<std::shared_ptr<mpp::UniformCollection>> mUniforms;
  std::vector<int32_t> mMaterialIndices;

  float mGlobalTime;

  wp::Logger* mwLogger;

public:
  typedef WorldBatchRenderer RendererType;

private:
  RendererType* mRenderer;

  std::shared_ptr<WorldTriangle3dDataProvider> mDataProvider;

public:
  WorldRenderer3d(
      wp::application::resourcesystem::ResourcePtr resource,
      wp::application::resourcesystem::ResourcePtr fragmentOverdrawMaterial,
      wp::Logger* logger,
      WorldSurfaceSet surfaceSet,
      SubMaterialResolver const* resolver,
      std::vector<WallRenderSurface> wallRenderSurfaces = {});

  virtual ~WorldRenderer3d();

  uint32_t getMeshIndexForMaterialHash(
      uint64_t hashValue, bool floor,
      std::optional<WallRenderVariant> const& variant = std::nullopt) const;

  // Pushes a Sub-material draft into the existing mesh bucket identified by
  // its baked hash. This deliberately changes uniforms only: no geometry or
  // batch allocation is needed while an editor slider is being dragged.
  void updateMaterialUniforms(
      uint64_t bakedMaterialHash, bool floor, int32_t materialIndex,
      bw::core::MaterialDefinitionData const& definition);

  void create(std::shared_ptr<WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  void addToScene(mpp::ScenePtr scene, bw::core::World const* world);

  // Applies MPP's line polygon mode to every material mesh in this world
  // surface set while preserving each mesh's other render flags.
  void setWireframe(bool wireframe);

  // Replaces every material bucket with the fixed-cost overdraw material and
  // enables blending so repeated fragments accumulate in the scene target.
  void setFragmentOverdraw(bool enabled);

  void update(
      glm::vec3 const& playerPosition,
      glm::vec3 const& lightPosition,
      bw::app::PlayerTorchOptions const& playerTorch,
      bool sortGeometryFrontToBack,
      int32_t materialIndexOverride,
      float materialScale,
      float farGridSize,
      SecondaryMaterialOptions const& secondaryMaterial,
      float frameTime);
};
