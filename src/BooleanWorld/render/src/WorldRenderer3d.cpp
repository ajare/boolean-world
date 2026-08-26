#include <mpp/ProgrammaticBasicMaterialStream.h>

#include <core/Defines.h>
#include <core/MaterialDefinition.h>
#include <core/World.h>

#include <common/GameDefines.h>

#include "WorldRenderer3d.h"

using namespace std;
using namespace wp::application::resourcesystem;

WorldRenderer3d::WorldRenderer3d(
    ResourcePtr resource, wp::Logger* logger, WorldSurfaceSet surfaceSet, SubMaterialResolver const* resolver)
    : mRenderer(nullptr),
      mMaterial(resource),
      mSurfaceSet(surfaceSet),
      mwResolver(resolver),
      mGlobalTime(0.0f),
      mwLogger(logger) {
}

WorldRenderer3d::~WorldRenderer3d() {
  // mSceneModel holds its own acquire() on the batch's Model resource (see
  // SceneModel3d's constructor), and Scene::m3dModels holds a second
  // shared_ptr to the same SceneModel3d. Unless both are dropped here,
  // before mRenderer is destroyed, that acquire is still outstanding when
  // Batch::~Batch() checks the resource's ref count - so its deleteResource
  // call is silently skipped and the Model resource (and its ResourceStream)
  // never leaves ResourceManager's cache, no matter how cleanly everything
  // else is torn down.
  if (mScene && mSceneModel) {
    mScene->remove3dModel(mSceneModel);
  }
  mSceneModel.reset();
  mScene.reset();

  delete mRenderer;
}

uint32_t WorldRenderer3d::getMeshIndexForMaterialHash(
    uint64_t hashValue, bool floor) const {
  auto worldBatch = mRenderer->getWorldBatch();

  return worldBatch->getMeshIndexForMaterialHash(hashValue, floor);
}

void WorldRenderer3d::updateMaterialUniforms(
    uint64_t bakedMaterialHash, bool floor, int32_t materialIndex,
    bw::core::MaterialDefinitionData const& definition) {
  auto worldBatch = mRenderer->getWorldBatch();
  // getMeshIndexForMaterialHash returns zero for a missing bucket, which is
  // also a valid first mesh. Confirm it exists before touching that bucket.
  if (!worldBatch->hasMeshForMaterialHash(bakedMaterialHash, floor)) {
    return;
  }
  auto meshIndex = worldBatch->getMeshIndexForMaterialHash(bakedMaterialHash, floor);
  if (meshIndex >= mUniforms.size() || !mUniforms[meshIndex]) {
    return;
  }

  auto const& uniforms = mUniforms[meshIndex];
  uniforms->updateUniform("MATERIAL_INDEX", materialIndex);
  uniforms->updateUniform("MATERIAL_PARAMS", definition.params.data());
}

void WorldRenderer3d::create(shared_ptr<WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  mDataProvider = dataProvider;

  // Renderer
  auto materialName = mMaterial->getQualifiedName();

  mRenderer = new RendererType(
      format(
          "World3d_{}_{}_", materialName,
          mSurfaceSet == WorldSurfaceSet::Horizontal ? "Horizontal" : "Walls"),
      mDataProvider,
      resourceMgr->getResource(materialName),
      renderSystem,
      resourceMgr,
      world,
      mSurfaceSet,
      mwResolver);

  mRenderer->create();

  mDataProvider->setMeshCount(static_pointer_cast<mpp::Model>(mRenderer->getModel())->getNumMeshes());
}

void WorldRenderer3d::addToScene(mpp::ScenePtr scene, bw::core::World const* world) {
  mScene = scene;
  mSceneModel = scene->add3dModel(mRenderer->getModel());

  auto params = mSceneModel->getParams();

  auto worldBatch = mRenderer->getWorldBatch();

  // Create uniforms for each material mesh.
  mUniforms.resize(worldBatch->getMaterialMeshCount(), nullptr);
  mMaterialIndices.resize(worldBatch->getMaterialMeshCount(), 0);
  mFloorMeshes.resize(worldBatch->getMaterialMeshCount(), false);

  auto initializeGlobalUniforms = [](
                                      mpp::UniformCollection& uniforms, bool floor) {
    uniforms.setUniform("VIEW_DISTANCE", BW_PLAYER_VIEW_DISTANCE);
    uniforms.setUniform("GLOBAL_TIME", 0.0f);
    uniforms.setUniform("PIXEL_SIZE", 1.0f / 32);
    uniforms.setUniform("FAR_GRID_SIZE", 0.5f);
    uniforms.setUniform("PLAYER_POSITION", glm::vec3{});
    uniforms.setUniform("LIGHT_POSITION", glm::vec3{});
    uniforms.setUniform("MATERIAL_SCALE", 32.0f);
    uniforms.setUniform("HEXAGON_RADIUS", 16.0f);
    uniforms.setUniform("HEXAGON_DEPTH", 0.5f);
    uniforms.setUniform("TILE_DEPTH_VARIATION_FACTOR", 0.1f);
    uniforms.setUniform("RUNNING_BOND_WIDTH_PERCENT", 50.0f);
    uniforms.setUniform("RUNNING_BOND_OFFSET_PERCENT", 50.0f);
    uniforms.setUniform("VORONOI_ROUNDED_EDGE_FACTOR", 0.25f);
    uniforms.setUniform("SECONDARY_MATERIAL_INDEX", int32_t{-1});
    uniforms.setUniform("USE_SECONDARY_MATERIAL", int32_t{0});
    uniforms.setUniform(
        "FLOOR_PATTERN",
        int32_t{floor ? static_cast<int32_t>(FloorPattern::Hexagon) : 0});
  };

  auto numPrimitives = world->getNumPrimitives();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = world->getPrimitive(i);
    auto const& properties = primitive->getProperties();

    if (mSurfaceSet == WorldSurfaceSet::Walls) {
      auto resolved = mwResolver->resolve(properties.wallMaterialId);
      auto hashValue = resolved.def.hash(resolved.materialIndex);
      auto meshIndex =
          worldBatch->getMeshIndexForMaterialHash(hashValue, false);
      if (mUniforms[meshIndex] == nullptr) {
        auto uniforms = make_shared<mpp::UniformCollection>();
        auto meshName = worldBatch->formatMeshName(hashValue, false);
        params->setMeshUniforms(meshName, uniforms);
        params->setMeshBlend(meshName, false);
        uniforms->setUniform(
            "MATERIAL_INDEX", (int32_t)resolved.materialIndex);
        uniforms->setUniform(
            "MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1,
            resolved.def.params.data());
        initializeGlobalUniforms(*uniforms, false);
        mUniforms[meshIndex] = uniforms;
        mMaterialIndices[meshIndex] =
            static_cast<int32_t>(resolved.materialIndex);
      }
      continue;
    }

    // Floor
    auto floorResolved = mwResolver->resolve(properties.floorMaterialId);
    auto hashValue = floorResolved.def.hash(floorResolved.materialIndex);
    auto meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue, true);

    if (mUniforms[meshIndex] == nullptr) {
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, true);

      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);

      uniforms->setUniform("MATERIAL_INDEX", (int32_t)floorResolved.materialIndex);
      uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, floorResolved.def.params.data());
      initializeGlobalUniforms(*uniforms, true);

      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(floorResolved.materialIndex);
      mFloorMeshes[meshIndex] = true;
    }

    // Ceiling
    auto ceilingResolved = mwResolver->resolve(properties.ceilingMaterialId);
    hashValue = ceilingResolved.def.hash(ceilingResolved.materialIndex);
    meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue, false);

    if (mUniforms[meshIndex] == nullptr) {
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, false);

      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);

      uniforms->setUniform("MATERIAL_INDEX", (int32_t)ceilingResolved.materialIndex);
      uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, ceilingResolved.def.params.data());
      initializeGlobalUniforms(*uniforms, false);

      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(ceilingResolved.materialIndex);
    }
  }

  if (mSurfaceSet == WorldSurfaceSet::Walls) {
    // The reserved, plain-white back-face material - see
    // WorldBatch::createModelStream, which guarantees this mesh bucket
    // exists regardless of any Primitive's authored material.
    bw::core::MaterialDefinition backMaterialDef;
    auto hashValue =
        backMaterialDef.data.hash(BW_WALL_BACK_FACE_MATERIAL_INDEX);
    auto meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue, false);
    if (mUniforms[meshIndex] == nullptr) {
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, false);
      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);
      uniforms->setUniform(
          "MATERIAL_INDEX", (int32_t)BW_WALL_BACK_FACE_MATERIAL_INDEX);
      uniforms->setUniform(
          "MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1,
          backMaterialDef.data.params.data());
      initializeGlobalUniforms(*uniforms, false);
      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(BW_WALL_BACK_FACE_MATERIAL_INDEX);
    }
  }
}

void WorldRenderer3d::update(
    glm::vec3 const& playerPosition,
    glm::vec3 const& lightPosition,
    int32_t materialIndexOverride,
    float materialScale,
    float farGridSize,
    FloorPatternOptions const& floorPattern,
    float frameTime) {
  mGlobalTime += frameTime;

  // Globals
  for (size_t i = 0; i < mUniforms.size(); ++i) {
    auto const& uc = mUniforms[i];
    if (uc == nullptr) {
      continue;
    }
    uc->updateUniform("VIEW_DISTANCE", BW_PLAYER_VIEW_DISTANCE);
    uc->updateUniform("GLOBAL_TIME", mGlobalTime);
    uc->updateUniform("PIXEL_SIZE", 1.0f / 32);
    uc->updateUniform("FAR_GRID_SIZE", farGridSize);
    uc->updateUniform("PLAYER_POSITION", playerPosition);
    uc->updateUniform("LIGHT_POSITION", lightPosition);
    uc->updateUniform("MATERIAL_SCALE", materialScale);
    uc->updateUniform("HEXAGON_RADIUS", floorPattern.radius);
    uc->updateUniform("HEXAGON_DEPTH", floorPattern.depth);
    uc->updateUniform(
        "TILE_DEPTH_VARIATION_FACTOR",
        floorPattern.tileDepthVariationFactor);
    uc->updateUniform(
        "RUNNING_BOND_WIDTH_PERCENT",
        floorPattern.runningBondWidthPercent);
    uc->updateUniform(
        "RUNNING_BOND_OFFSET_PERCENT",
        floorPattern.runningBondOffsetPercent);
    uc->updateUniform(
        "VORONOI_ROUNDED_EDGE_FACTOR",
        floorPattern.voronoiRoundedEdgeFactor);
    uc->updateUniform(
        "SECONDARY_MATERIAL_INDEX", floorPattern.secondaryMaterialIndex);
    uc->updateUniform(
        "USE_SECONDARY_MATERIAL",
        int32_t{floorPattern.usesSecondaryMaterial ? 1 : 0});
    uc->updateUniform(
        "FLOOR_PATTERN",
        int32_t{mFloorMeshes[i]
                    ? static_cast<int32_t>(floorPattern.pattern)
                    : 0});
    uc->updateUniform(
        "MATERIAL_INDEX",
        materialIndexOverride >= 0 ? materialIndexOverride : mMaterialIndices[i]);
  }

  mRenderer->update();
}