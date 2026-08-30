#pragma once

#include <memory>
#include <map>
#include <set>
#include <string>

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

inline constexpr float defaultLiquidReflectionMipLevel = 0.65f;

class WorldRenderer3d {
  wp::application::resourcesystem::ResourcePtr mMaterial;
  wp::application::resourcesystem::ResourcePtr mFragmentOverdrawMaterial;
  WorldSurfaceSet mSurfaceSet;
  bool mDeferToWaterPass;

  SubMaterialResolver const* mwResolver;
  std::vector<WallRenderSurface> mWallRenderSurfaces;

  mpp::ScenePtr mScene;

  mpp::SceneModel3dPtr mSceneModel;

  std::vector<std::shared_ptr<mpp::UniformCollection>> mUniforms;
  std::vector<int32_t> mMaterialIndices;

  // The material meshes this surface set draws blended in its own right -
  // currently the liquid surfaces, whose alpha is what lets the absorbed
  // geometry behind them show through. Every other mesh is opaque. The
  // diagnostic overdraw material blends every mesh; turning it off has to
  // restore this classification rather than making the whole world opaque.
  std::set<std::string> mBlendedMeshNames;
  // Meshes whose resolved material index is negative draw through the one
  // BooleanWorldRender-owned solid-magenta Debug material.
  mpp::ResourcePtr mDebugMaterial;
  std::set<std::string> mDebugMeshNames;
  // The renderer-owned 1x1 zero mask texture bound to TEX2 for every wall
  // mesh without a mask, keeping the shader's mask contract uniform.
  mpp::ResourcePtr mWallMaskZeroTexture;
  std::string mBatchNamePrefix;

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
      std::vector<WallRenderSurface> wallRenderSurfaces = {},
      bool deferToWaterPass = false,
      std::string batchNamePrefix = "World3d");

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

  void setWallRenderSurfaces(
      std::vector<WallRenderSurface> wallRenderSurfaces);

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
      float liquidEyeSurfaceHeight,
      glm::vec3 const& liquidExtinction,
      glm::vec3 const& liquidTint,
      std::optional<float> liquidReflectanceOverride,
      std::optional<float> liquidF0Override,
      float liquidReflectionMipLevel,
      bool liquidReflectionEnabled,
      bw::app::PlayerTorchOptions const& playerTorch,
      bool sortGeometryFrontToBack,
      int32_t materialIndexOverride,
      float materialScale,
      float pixelSize,
      SecondaryMaterialOptions const& secondaryMaterial,
      float frameTime);
};
