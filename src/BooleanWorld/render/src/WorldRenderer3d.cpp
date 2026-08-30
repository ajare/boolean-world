#include <algorithm>

#include <mpp/Material.h>
#include <mpp/Program.h>
#include <mpp/ProgrammaticBasicMaterialStream.h>
#include <mpp/ProgrammaticProgramStream.h>
#include <mpp/program/Parser.h>

#include <core/Defines.h>
#include <core/LiquidProperties.h>
#include <core/LiquidType.h>
#include <core/MaterialDefinition.h>
#include <core/World.h>

#include <common/GameDefines.h>

#include "WorldRenderer3d.h"

using namespace std;
using namespace wp::application::resourcesystem;

namespace {
constexpr char const* debugProgramName = "BooleanWorldRender.DebugMaterial.Program";
constexpr char const* debugMaterialName = "BooleanWorldRender.DebugMaterial";

// This material deliberately lives outside Resources.yaml and every
// ProcMaterial catalog. It shares the world's mesh contract but has no PBR
// controls: a negative material index is always an unlit solid magenta.
constexpr char const* debugVertexShader = R"(@@Version
void main()
{
    // The unlit fallback only needs position. Avoid forwarding world-specific
    // attributes because their semantic names differ between surface batches.
    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";
constexpr char const* debugFragmentShader = R"(@@Version
void main()
{
    // The forward graph may bind colour, bloom, GTAO normal, and liquid
    // retention attachments together. Keep all four locations active even
    // though this non-PBR fallback needs only its solid-magenta colour.
    @Out(vec4 COLOUR) = vec4(1.0, 0.0, 1.0, 1.0);
    @Out(vec4 BLOOM_MASK) = vec4(0.0);
    @Out(vec2 SHADING_NORMAL) = vec2(0.0);
    @Out(float LIQUID_RETENTION) = 0.0;
}
)";

mpp::ResourcePtr getOrCreateDebugMaterial(
    mpp::ResourceManager* resourceMgr,
    mpp::mesh::MeshSpecification const& specification) {
  auto program = resourceMgr->getResource(debugProgramName, true);
  if (!program) {
    auto parser = std::make_shared<mpp::program::Parser>();
    parser->setMeshSpecification(specification);
    parser->setVertexSource(debugVertexShader);
    parser->setFragmentSource(debugFragmentShader);
    auto stream = std::make_shared<mpp::ProgrammaticProgramStream>(resourceMgr);
    stream->setParser(parser);
    program = resourceMgr->declareResource(debugProgramName, stream).first;
  }

  auto material = resourceMgr->getResource(debugMaterialName, true);
  if (!material) {
    auto stream = std::make_shared<mpp::ProgrammaticBasicMaterialStream>(resourceMgr);
    stream->setProgram(debugProgramName);
    material = resourceMgr->declareResource(debugMaterialName, stream).first;
  }

  // Mesh parameter overrides do not acquire/create their material resource.
  // Unlike catalog materials, this internal resource has no application-level
  // Resource wrapper to do that for it, so make its program ready explicitly.
  material->create();
  return material;
}

char const* surfaceSetName(WorldSurfaceSet surfaceSet) {
  switch (surfaceSet) {
    case WorldSurfaceSet::Horizontal:
      return "Horizontal";
    case WorldSurfaceSet::Liquid:
      return "Liquid";
    case WorldSurfaceSet::Walls:
      return "Walls";
  }
  return "Unknown";
}
}  // namespace

WorldRenderer3d::WorldRenderer3d(
    ResourcePtr resource, ResourcePtr fragmentOverdrawMaterial,
    wp::Logger* logger, WorldSurfaceSet surfaceSet,
    SubMaterialResolver const* resolver,
    vector<WallRenderSurface> wallRenderSurfaces,
    bool deferToWaterPass,
    string batchNamePrefix)
    : mRenderer(nullptr),
      mMaterial(resource),
      mFragmentOverdrawMaterial(fragmentOverdrawMaterial),
      mSurfaceSet(surfaceSet),
      mDeferToWaterPass(deferToWaterPass),
      mBatchNamePrefix(move(batchNamePrefix)),
      mwResolver(resolver),
      mWallRenderSurfaces(move(wallRenderSurfaces)),
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
    uint64_t hashValue, bool floor,
    optional<WallRenderVariant> const& variant) const {
  auto worldBatch = mRenderer->getWorldBatch();
  if (variant && !worldBatch->hasMeshForMaterialHash(hashValue, floor, variant)) {
    throw logic_error(
        "Wall render variant was used for geometry without a seeded mesh bucket.");
  }
  return worldBatch->getMeshIndexForMaterialHash(hashValue, floor, variant);
}

namespace {

// The relief uniforms every mesh bucket carries, set from the Emboss preset
// resolved for that surface. Embossing varies independently of Sub-material,
// so these travel with the composed pair's bucket
// exactly as MATERIAL_INDEX/MATERIAL_PARAMS do.
void setEmbossUniforms(
    mpp::UniformCollection& uniforms, bw::core::EmbossData const& emboss) {
  uniforms.setUniform("EMBOSS_PATTERN", static_cast<int32_t>(emboss.pattern));
  uniforms.setUniform("EMBOSS_RADIUS", emboss.radius);
  uniforms.setUniform("EMBOSS_DEPTH", emboss.depth);
  uniforms.setUniform("EMBOSS_DEPTH_VARIATION", emboss.depthVariation);
  uniforms.setUniform("EMBOSS_RUNNING_BOND_WIDTH", emboss.runningBondWidth);
  uniforms.setUniform("EMBOSS_RUNNING_BOND_OFFSET", emboss.runningBondOffset);
  uniforms.setUniform("EMBOSS_VORONOI_ROUNDING", emboss.voronoiRounding);
}

void updateEmbossUniforms(
    mpp::UniformCollection& uniforms, bw::core::EmbossData const& emboss) {
  auto pattern = static_cast<int32_t>(emboss.pattern);
  uniforms.updateUniform("EMBOSS_PATTERN", pattern);
  uniforms.updateUniform("EMBOSS_RADIUS", emboss.radius);
  uniforms.updateUniform("EMBOSS_DEPTH", emboss.depth);
  uniforms.updateUniform("EMBOSS_DEPTH_VARIATION", emboss.depthVariation);
  uniforms.updateUniform("EMBOSS_RUNNING_BOND_WIDTH", emboss.runningBondWidth);
  uniforms.updateUniform("EMBOSS_RUNNING_BOND_OFFSET", emboss.runningBondOffset);
  uniforms.updateUniform("EMBOSS_VORONOI_ROUNDING", emboss.voronoiRounding);
}

}  // namespace

void WorldRenderer3d::updateMaterialUniforms(
    uint64_t bakedMaterialHash, bool floor, int32_t materialIndex,
    bw::core::MaterialDefinitionData const& definition) {
  auto worldBatch = mRenderer->getWorldBatch();
  auto updateMesh = [&](std::optional<WallRenderVariant> const& variant) {
    // getMeshIndexForMaterialHash returns zero for a missing bucket, which is
    // also a valid first mesh. Confirm it exists before touching that bucket.
    if (!worldBatch->hasMeshForMaterialHash(
            bakedMaterialHash, floor, variant)) {
      return;
    }
    auto meshIndex = worldBatch->getMeshIndexForMaterialHash(
        bakedMaterialHash, floor, variant);
    if (meshIndex >= mUniforms.size() || !mUniforms[meshIndex]) {
      return;
    }

    auto const& uniforms = mUniforms[meshIndex];
    uniforms->updateUniform("MATERIAL_INDEX", materialIndex);
    uniforms->updateUniform("MATERIAL_PARAMS", definition.params.data());
    // A draft's relief lands here too, so dragging an emboss slider in the
    // editor reads back immediately in the preview - the bucket keeps its
    // baked hash, only its uniforms change.
    updateEmbossUniforms(*uniforms, definition.emboss);
  };

  updateMesh(std::nullopt);
  if (mSurfaceSet != WorldSurfaceSet::Walls || floor) return;

  // Wall normal maps decorate a Sub-material bucket; they do not fork its
  // Technique or Embossing state. A preview draft must therefore reach every
  // mapped variant as well as the ordinary bucket, without touching the
  // variant's independently bound image uniforms and texture.
  for (auto const& surface : mWallRenderSurfaces) {
    auto resolved = mwResolver->resolve(
        surface.subMaterialId, surface.embossPresetId);
    if (resolved.def.hash(resolved.materialIndex) == bakedMaterialHash) {
      updateMesh(surface.variant);
    }
  }
}

void WorldRenderer3d::setWallRenderSurfaces(
    vector<WallRenderSurface> wallRenderSurfaces) {
  if (mRenderer != nullptr) {
    throw logic_error("Wall render surfaces must be configured before create().");
  }
  mWallRenderSurfaces = move(wallRenderSurfaces);
}

void WorldRenderer3d::create(shared_ptr<WorldTriangle3dDataProvider> dataProvider, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  mDataProvider = dataProvider;

  // Renderer
  auto materialName = mMaterial->getQualifiedName();

  mRenderer = new RendererType(
      format(
          "{}_{}_{}_", mBatchNamePrefix, materialName,
          surfaceSetName(mSurfaceSet)),
      mDataProvider,
      resourceMgr->getResource(materialName),
      renderSystem,
      resourceMgr,
      world,
      mSurfaceSet,
      mwResolver,
      mWallRenderSurfaces);

  mRenderer->create();

  // WorldBatch owns the exact vertex contract the generated model uses, so
  // the internal Debug program is compiled against that contract rather than
  // against any catalog or application-resource declaration.
  mDebugMaterial = getOrCreateDebugMaterial(
      resourceMgr, mRenderer->getWorldBatch()->getSpecification());

  mDataProvider->setMeshCount(static_pointer_cast<mpp::Model>(mRenderer->getModel())->getNumMeshes());
}

void WorldRenderer3d::addToScene(mpp::ScenePtr scene, bw::core::World const* world) {
  mScene = scene;
  mSceneModel = scene->add3dModel(mRenderer->getModel());
  // MPP's generated water graph defers whole scene models. Liquid is isolated
  // in its own renderer specifically so floors, ceilings, and walls remain in
  // the opaque scene while only this interface enters WaterScene.
  if (mSurfaceSet == WorldSurfaceSet::Liquid && mDeferToWaterPass) {
    mSceneModel->setDeferToWaterPass(true);
  }

  auto params = mSceneModel->getParams();

  auto worldBatch = mRenderer->getWorldBatch();
  auto useDebugMaterialFor =
      [&](std::string const& meshName, uint32_t materialIndex) {
        if (static_cast<int32_t>(materialIndex) >= 0) return;
        params->setMeshMaterial(meshName, mDebugMaterial);
        mDebugMeshNames.insert(meshName);
      };

  // Create uniforms for each material mesh.
  mUniforms.resize(worldBatch->getMaterialMeshCount(), nullptr);
  mMaterialIndices.resize(worldBatch->getMaterialMeshCount(), 0);

  auto initializeGlobalUniforms = [](mpp::UniformCollection& uniforms) {
    uniforms.setUniform("VIEW_DISTANCE", BW_PLAYER_VIEW_DISTANCE);
    uniforms.setUniform("GLOBAL_TIME", 0.0f);
    uniforms.setUniform("PIXEL_SIZE", 1.0f / 32);
    uniforms.setUniform("FAR_GRID_SIZE", 0.5f);
    uniforms.setUniform("PLAYER_POSITION", glm::vec3{});
    uniforms.setUniform("LIGHT_POSITION", glm::vec3{});
    uniforms.setUniform(
        "LIQUID_EYE_SURFACE_Z",
        WorldTriangle3dDataProvider::dryLiquidSurfaceHeight);
    uniforms.setUniform("LIQUID_EXTINCTION", glm::vec3{});
    uniforms.setUniform("LIQUID_TINT", glm::vec3{});
    uniforms.setUniform("LIQUID_REFLECTANCE", 0.0f);
    uniforms.setUniform("LIQUID_F0", 0.0f);
    uniforms.setUniform(
        "LIQUID_REFLECTION_MIP_LEVEL", defaultLiquidReflectionMipLevel);
    uniforms.setUniform("LIQUID_AMBIENT_TINT", glm::vec3{});
    uniforms.setUniform("LIQUID_WATER_PASS_ENABLED", int32_t{0});
    uniforms.setUniform("LIQUID_REFLECTION_ENABLED", int32_t{0});
    uniforms.setUniform("LIGHT_ATTENUATION_RADIUS", 192.0f);
    uniforms.setUniform("LIGHT_ATTENUATION_FALLOFF", 64.0f);
    uniforms.setUniform("MATERIAL_SCALE", 32.0f);
    uniforms.setUniform("SECONDARY_MATERIAL_INDEX", int32_t{-1});
    uniforms.setUniform("USE_SECONDARY_MATERIAL", int32_t{0});
    uniforms.setUniform("WALL_NORMAL_MAP_ENABLED", int32_t{0});
    uniforms.setUniform("WALL_NORMAL_MAP_STRENGTH", 1.0f);
    uniforms.setUniform("WALL_NORMAL_MAP_ASPECT_RATIO", 1.0f);
  };

  auto numPrimitives = world->getNumPrimitives();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = world->getPrimitive(i);
    auto const& properties = primitive->getProperties();

    if (mSurfaceSet == WorldSurfaceSet::Walls) {
      auto resolved = mwResolver->resolve(
          properties.wallMaterialId, properties.wallEmbossPresetId);
      auto hashValue = resolved.def.hash(resolved.materialIndex);
      auto meshIndex =
          worldBatch->getMeshIndexForMaterialHash(hashValue, false);
      if (mUniforms[meshIndex] == nullptr) {
        auto uniforms = make_shared<mpp::UniformCollection>();
        auto meshName = worldBatch->formatMeshName(hashValue, false);
        params->setMeshUniforms(meshName, uniforms);
        params->setMeshBlend(meshName, false);
        useDebugMaterialFor(meshName, resolved.materialIndex);
        uniforms->setUniform(
            "MATERIAL_INDEX", (int32_t)resolved.materialIndex);
        uniforms->setUniform(
            "MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1,
            resolved.def.params.data());
        setEmbossUniforms(*uniforms, resolved.def.emboss);
        initializeGlobalUniforms(*uniforms);
        mUniforms[meshIndex] = uniforms;
        mMaterialIndices[meshIndex] =
            static_cast<int32_t>(resolved.materialIndex);
      }
      continue;
    }
    // Liquid has only its reserved interface-material buckets, initialized
    // below. Looking authored floor/ceiling materials up in that model returns
    // the fallback mesh index and would poison the water bucket with ordinary
    // surface uniforms before its real initialization.
    if (mSurfaceSet == WorldSurfaceSet::Liquid) {
      continue;
    }

    // Floor
    auto floorResolved = mwResolver->resolve(
        properties.floorMaterialId, properties.floorEmbossPresetId);
    auto hashValue = floorResolved.def.hash(floorResolved.materialIndex);
    auto meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue, true);

    if (mUniforms[meshIndex] == nullptr) {
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, true);

      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);
      useDebugMaterialFor(meshName, floorResolved.materialIndex);

      uniforms->setUniform("MATERIAL_INDEX", (int32_t)floorResolved.materialIndex);
      uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, floorResolved.def.params.data());
      setEmbossUniforms(*uniforms, floorResolved.def.emboss);
      initializeGlobalUniforms(*uniforms);

      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(floorResolved.materialIndex);
    }

    // Ceiling
    auto ceilingResolved = mwResolver->resolve(
        properties.ceilingMaterialId, properties.ceilingEmbossPresetId);
    hashValue = ceilingResolved.def.hash(ceilingResolved.materialIndex);
    meshIndex = worldBatch->getMeshIndexForMaterialHash(hashValue, false);

    if (mUniforms[meshIndex] == nullptr) {
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, false);

      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);
      useDebugMaterialFor(meshName, ceilingResolved.materialIndex);

      uniforms->setUniform("MATERIAL_INDEX", (int32_t)ceilingResolved.materialIndex);
      uniforms->setUniform("MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1, ceilingResolved.def.params.data());
      setEmbossUniforms(*uniforms, ceilingResolved.def.emboss);
      initializeGlobalUniforms(*uniforms);

      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(ceilingResolved.materialIndex);
    }
  }

  if (mSurfaceSet == WorldSurfaceSet::Walls) {
    // Variants decorate only their own wall bucket.  Keeping this binding at
    // the mesh seam means one Sub-material can still back several textures or
    // uniform configurations without texture arrays or bindless state.
    for (auto const& surface : mWallRenderSurfaces) {
      auto const& variant = *surface.variant;
      auto resolved = mwResolver->resolve(
          surface.subMaterialId, surface.embossPresetId);
      auto hashValue = resolved.def.hash(resolved.materialIndex);
      auto meshIndex = worldBatch->getMeshIndexForMaterialHash(
          hashValue, false, variant);
      if (mUniforms[meshIndex] != nullptr) {
        continue;
      }
      auto uniforms = make_shared<mpp::UniformCollection>();
      auto meshName = worldBatch->formatMeshName(hashValue, false, variant);
      params->setMeshUniforms(meshName, uniforms);
      params->setMeshBlend(meshName, false);
      useDebugMaterialFor(meshName, resolved.materialIndex);
      if (variant.texture) {
        auto material = dynamic_pointer_cast<mpp::Material>(
            mMaterial->getMppResource());
        auto program = material
                           ? dynamic_pointer_cast<mpp::Program>(
                                 material->getProgram())
                           : nullptr;
        auto textureUnit = program
                               ? program->getSamplerUnit(variant.textureSampler)
                               : -1;
        if (textureUnit < 0) {
          throw logic_error(
              "Wall render variant texture sampler is unavailable: " +
              variant.textureSampler);
        }
        params->setMeshTexture(
            meshName, static_cast<uint32_t>(textureUnit), variant.texture);
      }
      uniforms->setUniform("MATERIAL_INDEX", static_cast<int32_t>(resolved.materialIndex));
      uniforms->setUniform(
          "MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1,
          resolved.def.params.data());
      setEmbossUniforms(*uniforms, resolved.def.emboss);
      initializeGlobalUniforms(*uniforms);
      if (variant.setUniforms) {
        variant.setUniforms(*uniforms);
      }
      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] = static_cast<int32_t>(resolved.materialIndex);
    }

    // The reserved, plain-white back-face material - see
    // WorldBatch::createModelStream, which guarantees this mesh bucket
    // exists regardless of any Primitive's authored material.
    bw::core::MaterialDefinition backMaterialDef{};
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
      setEmbossUniforms(*uniforms, backMaterialDef.data.emboss);
      initializeGlobalUniforms(*uniforms);
      mUniforms[meshIndex] = uniforms;
      mMaterialIndices[meshIndex] =
          static_cast<int32_t>(BW_WALL_BACK_FACE_MATERIAL_INDEX);
    }
  } else if (mSurfaceSet == WorldSurfaceSet::Liquid) {
    // Every liquid type's reserved material - see WorldBatch::createModel
    // Stream, which guarantees each one's mesh bucket exists regardless of
    // any Primitive's authored material.
    for (int32_t i = 0; i < bw::core::LiquidTypeCount; ++i) {
      auto materialIndex =
          bw::core::LiquidMaterialIndex(static_cast<bw::core::LiquidType>(i));
      bw::core::MaterialDefinition liquidMaterialDef{};
      auto hashValue = liquidMaterialDef.data.hash(materialIndex);
      auto meshIndex =
          worldBatch->getMeshIndexForMaterialHash(hashValue, true);
      if (mUniforms[meshIndex] == nullptr) {
        auto uniforms = make_shared<mpp::UniformCollection>();
        auto meshName = worldBatch->formatMeshName(hashValue, true);
        params->setMeshUniforms(meshName, uniforms);
        params->setMeshBlend(meshName, true);
        mBlendedMeshNames.insert(meshName);
        uniforms->setUniform("MATERIAL_INDEX", (int32_t)materialIndex);
        uniforms->setUniform(
            "MATERIAL_PARAMS", BW_MATERIAL_PARAMS_MAX, 1,
            liquidMaterialDef.data.params.data());
        setEmbossUniforms(*uniforms, liquidMaterialDef.data.emboss);
        initializeGlobalUniforms(*uniforms);
        auto const& liquid = bw::core::GetLiquidProperties(
            static_cast<bw::core::LiquidType>(i));
        // initializeGlobalUniforms created these entries with inert defaults;
        // update them rather than calling setUniform again (which deliberately
        // does not replace an existing UniformCollection entry).
        uniforms->updateUniform("LIQUID_REFLECTANCE", liquid.reflectance);
        uniforms->updateUniform("LIQUID_F0", liquid.f0);
        uniforms->updateUniform(
            "LIQUID_REFLECTION_MIP_LEVEL", defaultLiquidReflectionMipLevel);
        uniforms->updateUniform(
            "LIQUID_AMBIENT_TINT",
            glm::vec3{liquid.tint[0], liquid.tint[1], liquid.tint[2]});
        uniforms->updateUniform(
            "LIQUID_WATER_PASS_ENABLED",
            int32_t{mDeferToWaterPass ? 1 : 0});
        uniforms->updateUniform(
            "LIQUID_REFLECTION_ENABLED", int32_t{mDeferToWaterPass ? 1 : 0});
        mUniforms[meshIndex] = uniforms;
        mMaterialIndices[meshIndex] = static_cast<int32_t>(materialIndex);
      }
    }
  }
}

void WorldRenderer3d::setWireframe(bool wireframe) {
  if (!mSceneModel) {
    return;
  }
  auto params = mSceneModel->getParams();
  vector<pair<string, uint32_t>> meshFlags;
  for (auto const& [meshName, meshParams] : params->getMeshParams()) {
    if (!meshName.empty()) {
      meshFlags.emplace_back(meshName, meshParams.flags);
    }
  }
  for (auto const& [meshName, flags] : meshFlags) {
    auto updatedFlags = wireframe
                            ? flags | mpp::ModelRenderParams::Flag_Wireframe
                            : flags & ~mpp::ModelRenderParams::Flag_Wireframe;
    params->setMeshFlags(meshName, updatedFlags);
  }
}

void WorldRenderer3d::setFragmentOverdraw(bool enabled) {
  if (!mSceneModel) {
    return;
  }
  auto params = mSceneModel->getParams();
  auto material = enabled ? mFragmentOverdrawMaterial->getMppResource()
                          : mpp::ResourcePtr{};
  std::vector<std::string> meshNames;
  for (auto const& [meshName, meshParams] : params->getMeshParams()) {
    if (!meshName.empty()) {
      meshNames.push_back(meshName);
    }
  }
  for (auto const& meshName : meshNames) {
    params->setMeshMaterial(
        meshName, enabled ? material
                          : (mDebugMeshNames.contains(meshName)
                                 ? mDebugMaterial
                                 : mpp::ResourcePtr{}));
    // Turning the diagnostic off restores each mesh's own classification. A
    // liquid surface blends in its own right - forcing it opaque here drops
    // the alpha the world programs write and hides everything the liquid is
    // supposed to be seen through.
    params->setMeshBlend(
        meshName, enabled || mBlendedMeshNames.count(meshName) != 0);
    // The diagnostic material blends to accumulate fragments, but the source
    // world surface is opaque and must still populate an enabled depth prepass.
    // Clearing the override restores normal opaque/blended classification.
    params->setMeshDepthPrepass(
        meshName, enabled ? std::optional<bool>{true} : std::nullopt);
  }
}

void WorldRenderer3d::update(
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
    float farGridSize,
    SecondaryMaterialOptions const& secondaryMaterial,
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
    uc->updateUniform("LIQUID_EYE_SURFACE_Z", liquidEyeSurfaceHeight);
    uc->updateUniform("LIQUID_EXTINCTION", liquidExtinction);
    uc->updateUniform("LIQUID_TINT", liquidTint);
    if (mSurfaceSet == WorldSurfaceSet::Liquid) {
      uc->updateUniform(
          "LIQUID_REFLECTION_MIP_LEVEL",
          clamp(liquidReflectionMipLevel, 0.0f, 4.0f));
      uc->updateUniform(
          "LIQUID_REFLECTION_ENABLED",
          int32_t{mDeferToWaterPass && liquidReflectionEnabled ? 1 : 0});
      for (int32_t liquidIndex = 0;
           liquidIndex < bw::core::LiquidTypeCount; ++liquidIndex) {
        auto liquidType = static_cast<bw::core::LiquidType>(liquidIndex);
        if (mMaterialIndices[i] !=
            static_cast<int32_t>(bw::core::LiquidMaterialIndex(liquidType))) {
          continue;
        }
        auto const& properties = bw::core::GetLiquidProperties(liquidType);
        uc->updateUniform(
            "LIQUID_REFLECTANCE",
            liquidReflectanceOverride.value_or(properties.reflectance));
        uc->updateUniform(
            "LIQUID_F0", liquidF0Override.value_or(properties.f0));
        break;
      }
    }
    uc->updateUniform(
        "LIGHT_ATTENUATION_RADIUS", playerTorch.attenuationRadius);
    uc->updateUniform(
        "LIGHT_ATTENUATION_FALLOFF", playerTorch.attenuationFalloff);
    uc->updateUniform("MATERIAL_SCALE", materialScale);
    // Relief is per composed surface bucket and set once from its preset;
    // only the debug secondary-material choice is still global.
    uc->updateUniform(
        "SECONDARY_MATERIAL_INDEX", secondaryMaterial.materialIndex);
    uc->updateUniform(
        "USE_SECONDARY_MATERIAL", int32_t{secondaryMaterial.enabled ? 1 : 0});
    uc->updateUniform(
        "MATERIAL_INDEX",
        materialIndexOverride >= 0 ? materialIndexOverride : mMaterialIndices[i]);
  }

  mDataProvider->orderTrianglesForView(
      playerPosition,
      mSurfaceSet == WorldSurfaceSet::Liquid
          ? WorldTriangle3dDataProvider::TriangleOrder::BackToFront
      : sortGeometryFrontToBack
          ? WorldTriangle3dDataProvider::TriangleOrder::FrontToBack
          : WorldTriangle3dDataProvider::TriangleOrder::Authored);
  mRenderer->update();
}