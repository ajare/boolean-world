#pragma once

#include <memory>
#include <map>

#include <glm/vec3.hpp>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/UniformCollection.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/common/Logger.h>

#include "FloorPatternOptions.h"
#include "SubMaterialResolver.h"
#include "WorldBatchRenderer.h"
#include "WorldTriangle3dDataProvider.h"

class WorldRenderer3d {
  wp::application::resourcesystem::ResourcePtr mMaterial;
  WorldSurfaceSet mSurfaceSet;

  SubMaterialResolver const* mwResolver;

  mpp::ScenePtr mScene;

  mpp::SceneModel3dPtr mSceneModel;

  std::vector<std::shared_ptr<mpp::UniformCollection>> mUniforms;
  std::vector<int32_t> mMaterialIndices;
  std::vector<bool> mFloorMeshes;

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
      wp::Logger* logger,
      WorldSurfaceSet surfaceSet,
      SubMaterialResolver const* resolver);

  virtual ~WorldRenderer3d();

  uint32_t getMeshIndexForMaterialHash(uint64_t hashValue, bool floor) const;

  // Pushes a Sub-material draft into the existing mesh bucket identified by
  // its baked hash. This deliberately changes uniforms only: no geometry or
  // batch allocation is needed while an editor slider is being dragged.
  void updateMaterialUniforms(
      uint64_t bakedMaterialHash, bool floor, int32_t materialIndex,
      bw::core::MaterialDefinitionData const& definition);

  void create(std::shared_ptr<WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  void addToScene(mpp::ScenePtr scene, bw::core::World const* world);

  void update(
      glm::vec3 const& playerPosition,
      glm::vec3 const& lightPosition,
      int32_t materialIndexOverride,
      float materialScale,
      float farGridSize,
      FloorPatternOptions const& floorPattern,
      float frameTime);
};
