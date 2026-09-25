#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

#include <common/GameDefines.h>

#include <core/Defines.h>
#include <core/Phantom.h>
#include <core/LiquidProperties.h>
#include <core/LiquidType.h>
#include <core/MaterialDefinition.h>
#include <core/World.h>

#include <mpp/RenderTexture.h>
#include <willpower/application/resourcesystem/ImageResource.h>

#include "WorldRenderer.h"
#include "PortalLight.h"
#include "TriplanarWallRenderData.h"
#include "WallMaskRenderData.h"
#include "WallNormalMapRenderData.h"

using namespace std;

namespace {

// world_pbr.frag multiplies a surface's own colour by its vertex colour
// before lighting, so anything wanting the material rendered as authored
// passes white. The editor's 3D preview is the one caller that does not,
// tinting the surface the viewer is looking at.
constexpr uint32_t untintedVertexColour = 0xffffffffu;
// Full red/green, 35% blue: the preview's established looked-at tint.
constexpr uint32_t lookedAtVertexColour = 0xff59ffffu;
// Phase 1 has no liquid-interface contribution; absorption belongs to the
// submerged surface behind it. The liquid mesh remains for reflection later.
constexpr uint32_t transparentVertexColour = 0x00ffffffu;

string normalMapIdentity(bw::core::WallNormalMapOverride::ImageData const& image) {
  ostringstream result;
  result << "normal-map-v1-";
  for (auto byte : image.resourceName) {
    result << hex << setw(2) << setfill('0')
           << static_cast<unsigned>(static_cast<unsigned char>(byte));
  }
  result << '-' << hex << bit_cast<uint32_t>(image.repeat) << '-'
         << bit_cast<uint32_t>(image.strength);
  return result.str();
}

// The resolved texture plus its authored tuning for one wall image. These are
// shared, keyed payloads: a normal map (and a mask, when present) may back
// several Sub-material buckets without duplicating the GPU texture.
struct NormalMapPayload {
  mpp::ResourcePtr texture;
  float strength{1.0f};
  float aspectRatio{1.0f};
};

bool portalFallbackBelongsTo(
    bw::core::arr::DetailTriangle const& triangle,
    bw::core::ResolvedAperture const& aperture) {
  wp::Vector2 centre{};
  float elevation{};
  for (auto const& vertex : triangle.v) {
    centre += {vertex.position[0], vertex.position[1]};
    elevation += vertex.position[2];
  }
  centre /= 3.0f;
  elevation /= 3.0f;
  auto offset = centre - aperture.centre;
  return std::abs(offset.dot(aperture.front)) <= 0.01f &&
         std::abs(offset.dot(aperture.tangent)) <= aperture.width * 0.5f + 0.01f &&
         elevation >= aperture.bottom - 0.01f &&
         elevation <= aperture.top + 0.01f;
}

struct MaskPayload {
  mpp::ResourcePtr texture;
  uint8_t channel{0};
  bw::core::WallMaskOverride::BlendParameters blendParameters{};
  bw::core::WallMaskOverride::BlendColour blendColour{1.0f, 1.0f, 1.0f};
  float aspectRatio{1.0f};
};

}  // namespace

class WorldRenderer::PreparedWorldRenderData {
  friend class WorldRenderer;

  bw::core::WorldDataPtr worldData;
  std::array<DataProvider, 3> providers;
};

// The primary Phantom scene contains only apertures, but retains the real
// world's shadow casters. Child views execute the complete Euclidean pipeline
// (including water/reflections/AO), rather than the reduced Portal colour pass.
class WorldRenderer::PhantomRenderState {
public:
  struct ApertureScene : mpp::Scene {
    mpp::ScenePtr world;
    ApertureScene(mpp::RenderSystem* system, mpp::ScenePtr source)
        : mpp::Scene(system), world(std::move(source)) {}
    std::vector<mpp::SceneModel3dPtr> get3dModelsInSphere(
        glm::vec3 const& centre, float radius) override {
      return world->get3dModelsInSphere(centre, radius);
    }
  };
  mpp::RenderSystem* system{};
  mpp::ScenePtr scene;
  Renderer renderer;
  DataProvider provider;
  std::vector<uint32_t> walls;
  std::map<uint32_t, mpp::RenderPipelinePtr> pipelines;
  std::weak_ptr<mpp::RenderPipeline> hostPipeline;
  void clearPipelines() {
    for (auto const& [wall, pipeline] : pipelines)
      system->removeRenderPipeline(pipeline->getName());
    pipelines.clear();
  }
  ~PhantomRenderState() {
    // Pipelines retain their last submitted models; release them before batches.
    clearPipelines();
    renderer.reset();
  }
};

WorldRenderer::WorldRenderer(
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::Logger* logger,
    bw::app::RenderTextureFilter renderTextureFilter,
    bw::app::HorizontalMaterials horizontalMaterials,
    vector<WallRenderSurface> wallRenderSurfaces,
    WallRenderVariantResolver wallRenderVariantResolver,
    string worldResourceNamespace,
    bool deferLiquidToWaterPass,
    string batchNamePrefix)
    : mSubMaterialResolver(resourceMgr),
      mBakedSubMaterialResolver(resourceMgr),
      mSurfaceMaterialResolver(
          mSubMaterialResolver, resourceMgr, worldResourceNamespace),
      mBakedSurfaceMaterialResolver(
          mBakedSubMaterialResolver, resourceMgr, worldResourceNamespace),
      mResourceMgr(resourceMgr),
      mWorldResourceNamespace(move(worldResourceNamespace)),
      mBatchNamePrefix(move(batchNamePrefix)),
      mWallRenderSurfaces(move(wallRenderSurfaces)),
      mWallRenderVariantResolver(move(wallRenderVariantResolver)),
      mWorldHasChanged(true),
      mwLogger(logger),
      mRenderTextureFilter(renderTextureFilter) {
  auto material3d = resourceMgr->getResource("Material.Default", "World");
  auto horizontalMaterial = horizontalMaterials ==
                                    bw::app::HorizontalMaterials::TwoDimensional
                                ? resourceMgr->getResource(
                                      "Material.Horizontal2d", "World")
                                : material3d;
  auto fragmentOverdrawMaterial =
      resourceMgr->getResource("Material.FragmentOverdraw", "World");

  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           horizontalMaterial, fragmentOverdrawMaterial, mwLogger,
           WorldSurfaceSet::Horizontal,
           &mSurfaceMaterialResolver, vector<WallRenderSurface>{}, false,
           mBatchNamePrefix),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Horizontal});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, fragmentOverdrawMaterial, mwLogger,
           WorldSurfaceSet::Liquid,
           &mSurfaceMaterialResolver, vector<WallRenderSurface>{},
           deferLiquidToWaterPass, mBatchNamePrefix),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Liquid});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, fragmentOverdrawMaterial, mwLogger,
           WorldSurfaceSet::Walls,
           &mSurfaceMaterialResolver,
           mWallRenderSurfaces, false, mBatchNamePrefix),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Walls});
}

WorldRenderer::~WorldRenderer() {
}

void WorldRenderer::createRenderTargets(mpp::RenderSystem* renderSystem) {
  mpp::RenderTextureOptions options;

  // The composite stretches each target over the whole screen. MPP takes the
  // OpenGL wire values here: 0x2600 is GL_NEAREST, 0x2601 is GL_LINEAR and
  // 0x812F is GL_CLAMP_TO_EDGE.
  auto filter = mRenderTextureFilter == bw::app::RenderTextureFilter::Nearest
                    ? 0x2600
                    : 0x2601;
  options.params.minFilter = filter;
  options.params.magFilter = filter;
  options.params.wrap = 0x812F;

  for (auto renderScale : bw::app::allRenderScales) {
    auto size = bw::app::renderTargetSize(
        renderSystem->getWindowWidth(),
        renderSystem->getWindowHeight(),
        renderScale);
    auto name = "BooleanWorld.WorldTarget." +
                std::string(bw::app::renderScaleName(renderScale));
    mWorldTargets[bw::app::renderScaleIndex(renderScale)] =
        renderSystem->createRenderTexture(name, size.width, size.height, options);
  }
}

mpp::RenderTargetPtr const& WorldRenderer::getRenderTarget(
    bw::app::RenderScale renderScale) const {
  return mWorldTargets[bw::app::renderScaleIndex(renderScale)];
}

WorldRenderer::RenderTargets WorldRenderer::detachRenderTargets() {
  return std::move(mWorldTargets);
}

WorldRenderer::PreparedWorldRenderDataPtr
WorldRenderer::prepareWorldRenderData(
    bw::core::WorldDataPtr worldData) {
  if (!worldData) {
    throw std::invalid_argument("world render data requires a World snapshot");
  }

  auto prepared = std::make_shared<PreparedWorldRenderData>();
  prepared->worldData = std::move(worldData);
  for (size_t index = 0; index < prepared->providers.size(); ++index) {
    prepared->providers[index] =
        std::make_shared<WorldTriangle3dDataProvider>();
    prepared->providers[index]->setMeshCount(
        mMaterialRenderers[index].dataProvider->getNumMeshes());
  }

  updateHorizontalDataProvider(
      *prepared->worldData, -1, false, prepared->providers[0]);
  updateLiquidDataProvider(*prepared->worldData, prepared->providers[1]);
  updateWallDataProvider(*prepared->worldData, prepared->providers[2]);
  return prepared;
}

void WorldRenderer::publishWorldRenderData(
    PreparedWorldRenderDataPtr const& prepared) {
  if (!prepared) {
    throw std::invalid_argument("prepared world render data is missing");
  }
  for (size_t index = 0; index < prepared->providers.size(); ++index) {
    mMaterialRenderers[index].dataProvider->replaceData(
        *prepared->providers[index]);
  }
  // All camera-dependent choices are uniforms; published buffers are final.
  mWorldHasChanged = false;
  mPhantomGeometryDirty = true;
  mHighlightedTriangle = -1;
  mHighlightedCeiling = false;
}

void WorldRenderer::create(mpp::ScenePtr scene, bw::core::World* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  mwWorld = world;
  mScene = scene;
  mRenderSystem = renderSystem;
  mRenderResourceMgr = resourceMgr;

  // Turn authored ImageResource references into stable variant buckets before
  // the wall batch is created. Disabled and Unset intentionally seed nothing.
  // A mask shares the normal map's repeat and UVs when one is present; a mask
  // without a normal map owns a single-tile repeat of its own.
  auto loadWallImage = [&](std::string const& resourceName,
                           char const* kind) {
    string namesp;
    string name;
    wp::application::resourcesystem::Resource::splitName(
        resourceName, mWorldResourceNamespace, &namesp, &name);
    auto resource = mResourceMgr->getResource(name, namesp);
    auto imageResource = dynamic_pointer_cast<
        wp::application::resourcesystem::ImageResource>(resource);
    if (!imageResource || !mResourceMgr->isResourceLoaded(resource)) {
      throw runtime_error(string{"Wall "} + kind + " resource '" +
                          resourceName + "' is not a loaded ImageResource.");
    }
    if (imageResource->getWidth() <= 0 ||
        imageResource->getHeight() <= 0 ||
        imageResource->getWidth() > 8192 ||
        imageResource->getHeight() > 8192 ||
        (imageResource->getNumChannels() != 3 &&
         imageResource->getNumChannels() != 4)) {
      throw runtime_error(string{"Wall "} + kind +
                          " ImageResource '" + resourceName +
                          "' has unsupported dimensions or channels.");
    }
    return imageResource;
  };

  vector<pair<bw::core::SurfaceMaterialReference, string>> wallSurfaceMaterials;
  map<string, NormalMapPayload> normalMapPayloads;
  map<string, MaskPayload> maskPayloads;
  set<pair<string, string>> imageCombinations;  // (normal map, mask or empty)
  for (uint32_t primitiveIndex = 0;
       primitiveIndex < world->getNumPrimitives(); ++primitiveIndex) {
    auto* primitive = world->getPrimitive(primitiveIndex);
    auto const& properties = primitive->getProperties();
    auto wallSurface = pair{
        properties.wallMaterial, properties.wallEmbossPresetId};
    if (find(wallSurfaceMaterials.begin(), wallSurfaceMaterials.end(),
             wallSurface) == wallSurfaceMaterials.end()) {
      wallSurfaceMaterials.push_back(move(wallSurface));
    }
    for (auto const& polygon : primitive->getVertices()) {
      for (auto const& ring : polygon) {
        for (auto const& vertex : ring) {
          auto normalMap = vertex.edgeNormalMap.imageData();
          auto mask = vertex.edgeWallMask.imageData();
          if (!normalMap && !mask) continue;

          auto normalMapId = string{};
          if (normalMap) {
            normalMapId = normalMapIdentity(*normalMap);
            if (!normalMapPayloads.contains(normalMapId)) {
              auto imageResource =
                  loadWallImage(normalMap->resourceName, "normal-map");
              normalMapPayloads.emplace(
                  normalMapId,
                  NormalMapPayload{
                      imageResource->getMppResource(), normalMap->strength,
                      static_cast<float>(imageResource->getWidth()) /
                          imageResource->getHeight()});
            }
          }

          auto maskId = string{};
          if (mask) {
            maskId = maskIdentity(*mask);
            if (!maskPayloads.contains(maskId)) {
              auto imageResource = loadWallImage(mask->resourceName, "mask");
              if (mask->channel == 3 &&  // Alpha needs a fourth channel.
                  imageResource->getNumChannels() != 4) {
                throw runtime_error(
                    "Wall mask ImageResource '" + mask->resourceName +
                    "' has no alpha channel for the selected Alpha mask channel.");
              }
              maskPayloads.emplace(
                  maskId,
                  MaskPayload{
                      imageResource->getMppResource(), mask->channel,
                      mask->blendParameters, mask->blendColour,
                      static_cast<float>(imageResource->getWidth()) /
                          imageResource->getHeight()});
            }
          }
          imageCombinations.emplace(normalMapId, maskId);
        }
      }
    }
  }

  // Build the combined variants. The mask identity is concatenated onto the
  // normal map's (a mask without a normal map contributes just the mask
  // identity), so walls differing in either image resolve to distinct buckets.
  for (auto const& [normalMapId, maskId] : imageCombinations) {
    auto identity = normalMapId + maskId;
    WallRenderVariant variant;
    variant.identity = identity;

    if (!normalMapId.empty()) {
      auto const& normalMap = normalMapPayloads.at(normalMapId);
      variant.textureSampler = "TEX1";
      variant.texture = normalMap.texture;
      auto strength = normalMap.strength;
      auto aspectRatio = normalMap.aspectRatio;
      variant.setUniforms =
          [strength, aspectRatio](mpp::UniformCollection& uniforms) {
            uniforms.updateUniform("WALL_NORMAL_MAP_ENABLED", int32_t{1});
            uniforms.updateUniform("WALL_NORMAL_MAP_STRENGTH", strength);
            uniforms.updateUniform(
                "WALL_NORMAL_MAP_ASPECT_RATIO", aspectRatio);
          };
    }

    if (!maskId.empty()) {
      auto const& mask = maskPayloads.at(maskId);
      variant.maskTextureSampler = "TEX2";
      variant.maskTexture = mask.texture;
      auto channel = static_cast<int32_t>(mask.channel);
      auto blendParameters = mask.blendParameters;
      auto blendColour = mask.blendColour;
      auto maskAspectRatio = mask.aspectRatio;
      auto hasNormalMap = !normalMapId.empty();
      variant.setMaskUniforms =
          [channel, blendParameters, blendColour, maskAspectRatio,
           hasNormalMap](mpp::UniformCollection& uniforms) {
            uniforms.updateUniform("WALL_MASK_ENABLED", int32_t{1});
            uniforms.updateUniform("WALL_MASK_CHANNEL", channel);
            uniforms.updateUniform(
                "WALL_MASK_BLEND_PARAMS", blendParameters.data());
            uniforms.updateUniform(
                "WALL_MASK_BLEND_COLOUR",
                glm::vec3{blendColour[0], blendColour[1], blendColour[2]});
            // A mask without a normal map owns the wall-image aspect ratio
            // the shared UVs are corrected by; the normal map stays off.
            if (!hasNormalMap) {
              uniforms.updateUniform(
                  "WALL_NORMAL_MAP_ENABLED", int32_t{0});
              uniforms.updateUniform(
                  "WALL_NORMAL_MAP_ASPECT_RATIO", maskAspectRatio);
            }
          };
    }
    mWallImageVariants.emplace(identity, move(variant));
  }

  // The winning override and the wall's Surface material can come from
  // different Primitives in the fold. Predeclare their complete cross-product
  // so a resolved Image always has a bucket; Unset and Disabled intentionally
  // use the single existing unmapped bucket.
  for (auto const* layer : world->getLayers()) {
    for (auto const& pair : layer->getPortalPairs()) {
      for (auto const& endpoint : pair.getEndpoints()) {
        mPortalEndpointBuckets.emplace(
            PortalEndpointKey{
                layer->getId(), pair.getId(), endpoint.getId()},
            static_cast<uint32_t>(mPortalEndpointBuckets.size()));
      }
    }
  }
  for (auto const& [material, embossPresetId] : wallSurfaceMaterials) {
    for (auto const& [identity, variant] : mWallImageVariants) {
      BW_UNUSED(identity);
      mWallRenderSurfaces.push_back(
          {material, variant, embossPresetId});
    }
    // Buckets belong to stable endpoints, not temporary recursive-view slots.
    // Visibility changes texture bindings only, never triangle membership.
    for (uint32_t bucket = 0; bucket < mPortalEndpointBuckets.size(); ++bucket) {
      mWallRenderSurfaces.push_back(
          {material,
           WallRenderVariant{
               .identity = portalWallRenderVariantIdentity(bucket)},
           embossPresetId});
    }
  }
  auto fallbackResolver = move(mWallRenderVariantResolver);
  mWallRenderVariantResolver =
      [this, fallbackResolver = move(fallbackResolver)](
          bw::core::arr::ArrangementWall const& wall)
      -> optional<WallRenderVariant> {
    auto normalMap = wall.normalMapOverride.imageData();
    auto mask = wall.wallMaskOverride.imageData();
    if (!normalMap && !mask) {
      return fallbackResolver ? fallbackResolver(wall)
                              : optional<WallRenderVariant>{};
    }
    auto identity =
        (normalMap ? normalMapIdentity(*normalMap) : string{}) +
        (mask ? maskIdentity(*mask) : string{});
    auto found = mWallImageVariants.find(identity);
    if (found != mWallImageVariants.end()) return found->second;
    return fallbackResolver ? fallbackResolver(wall)
                            : optional<WallRenderVariant>{};
  };
  mMaterialRenderers[2].renderer->setWallRenderSurfaces(mWallRenderSurfaces);
  // Material lookup stays outside core geometry. Capture an immutable resolver
  // snapshot so generation workers see only plain dimensions and never a
  // ProcMaterial catalog or render-side object.
  world->getWorldDataGenerator()->setChipParametersResolver(
      mSubMaterialResolver.chipParametersResolver());
  for (auto& item : mMaterialRenderers) {
    item.renderer->create(
        item.dataProvider, world, renderSystem, resourceMgr);
    item.renderer->addToScene(scene, world);
    item.renderer->setWireframe(mWireframe);
    item.renderer->setFragmentOverdraw(mFragmentOverdraw);
  }
}

void WorldRenderer::setWorldChanged() {
  mWorldHasChanged = true;
  mPhantomGeometryDirty = true;
}

uint32_t WorldRenderer::getSurfaceTriangleCount(
    WorldSurfaceSet surfaceSet) const {
  for (auto const& item : mMaterialRenderers) {
    if (item.surfaceSet == surfaceSet) {
      return item.dataProvider->getNumTriangles();
    }
  }
  return 0;
}

void WorldRenderer::setWireframe(bool wireframe) {
  if (wireframe == mWireframe) {
    return;
  }
  mWireframe = wireframe;
  for (auto const& item : mMaterialRenderers) {
    item.renderer->setWireframe(wireframe);
  }
}

void WorldRenderer::setFragmentOverdraw(bool enabled) {
  if (enabled == mFragmentOverdraw) {
    return;
  }
  mFragmentOverdraw = enabled;
  for (auto const& item : mMaterialRenderers) {
    item.renderer->setFragmentOverdraw(enabled);
  }
}

void WorldRenderer::updateSubMaterialDraft(
    string const& subMaterialId, int32_t materialIndex,
    bw::core::MaterialDefinitionData const& definition) {
  // One Sub-material can back several preset-specific buckets. Update each
  // baked pair while preserving the Embossing resolved from that pair: a
  // Sub-material draft must not become an alternate source of relief.
  if (!mwWorld) return;
  set<tuple<string, bool>> surfaces;
  for (uint32_t i = 0; i < mwWorld->getNumPrimitives(); ++i) {
    auto const& properties = mwWorld->getPrimitive(i)->getProperties();
    if (properties.floorMaterial.kind ==
            bw::core::SurfaceMaterialKind::SubMaterial &&
        properties.floorMaterial.reference == subMaterialId)
      surfaces.emplace(properties.floorEmbossPresetId, true);
    if (properties.ceilingMaterial.kind ==
            bw::core::SurfaceMaterialKind::SubMaterial &&
        properties.ceilingMaterial.reference == subMaterialId)
      surfaces.emplace(properties.ceilingEmbossPresetId, false);
    if (properties.wallMaterial.kind ==
            bw::core::SurfaceMaterialKind::SubMaterial &&
        properties.wallMaterial.reference == subMaterialId)
      surfaces.emplace(properties.wallEmbossPresetId, false);
  }
  for (auto const& [presetId, floor] : surfaces) {
    auto baked = mBakedSubMaterialResolver.resolve(subMaterialId, presetId);
    auto composed = definition;
    composed.emboss = mSubMaterialResolver.resolve(
                                              subMaterialId, presetId)
                          .def.emboss;
    auto bakedHash = baked.def.hash(baked.materialIndex);
    for (auto const& item : mMaterialRenderers) {
      if ((floor && item.surfaceSet == WorldSurfaceSet::Horizontal) ||
          (!floor && item.surfaceSet != WorldSurfaceSet::Liquid)) {
        item.renderer->updateMaterialUniforms(
            bakedHash, floor, materialIndex, composed);
      }
    }
  }
}

void WorldRenderer::updateEmbossPresetDraft(
    string const& embossPresetId, bw::core::EmbossData const& emboss) {
  if (!mwWorld || embossPresetId.empty()) return;

  set<tuple<bw::core::SurfaceMaterialKind, string, bool>> surfaces;
  for (uint32_t i = 0; i < mwWorld->getNumPrimitives(); ++i) {
    auto const& properties = mwWorld->getPrimitive(i)->getProperties();
    if (properties.floorEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.floorMaterial.kind,
                       properties.floorMaterial.reference, true);
    if (properties.ceilingEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.ceilingMaterial.kind,
                       properties.ceilingMaterial.reference, false);
    if (properties.wallEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.wallMaterial.kind,
                       properties.wallMaterial.reference, false);
  }

  for (auto const& [kind, reference, floor] : surfaces) {
    auto material = bw::core::SurfaceMaterialReference{kind, reference};
    auto baked = mBakedSurfaceMaterialResolver.resolve(
        material, embossPresetId);
    auto composed = mSurfaceMaterialResolver.resolve(
        material, embossPresetId);
    composed.def.emboss = emboss;
    auto bakedHash = baked.hash();
    for (auto const& item : mMaterialRenderers) {
      if ((floor && item.surfaceSet == WorldSurfaceSet::Horizontal) ||
          (!floor && item.surfaceSet != WorldSurfaceSet::Liquid)) {
        item.renderer->updateMaterialUniforms(
            bakedHash, floor, composed.materialIndex, composed.def);
      }
    }
  }
}

void WorldRenderer::reloadSubMaterialResolver(
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  mSubMaterialResolver = SubMaterialResolver(resourceMgr);
  if (mwWorld) {
    mwWorld->getWorldDataGenerator()->setChipParametersResolver(
        mSubMaterialResolver.chipParametersResolver());
  }
}

uint32_t WorldRenderer::addVertexToDataProvider(
    DataProvider dataProvider, uint32_t meshIndex, float px, float py,
    float pz, float nx, float ny, float nz, float u, float v, uint32_t c,
    float liquidSurfaceHeight, float surfaceUpX, float surfaceUpY,
    float surfaceUpZ,
    optional<array<float, 3>> const& projectionNormal) {
  auto projection = projectionNormal.value_or(
      array<float, 3>{surfaceUpX, surfaceUpY, surfaceUpZ});
  WorldTriangle3dDataProvider::DrawVert vertex{
      {px, py, pz}, {nx, ny, nz},
      {u, v, 0.0f, WorldTriangle3dDataProvider::dryLiquidSurfaceHeight}, c,
      {projection[0], projection[1], projection[2]}, liquidSurfaceHeight};
  return dataProvider->addVertex(meshIndex, vertex);
}

void WorldRenderer::addDetailTriangleToDataProvider(
    DataProvider dataProvider,
    uint32_t meshIndex,
    bw::core::arr::DetailTriangle const& triangle,
    bool mirrored,
    uint32_t colour,
    float liquidSurfaceHeight,
    float surfaceUpX,
    float surfaceUpY,
    float surfaceUpZ) {
  // Arrangement space keeps height in z and renderer space keeps it in y,
  // mapping (x, y, z) to (x, z, -y) - a rotation, not a reflection, so a
  // triangle wound counter-clockwise about its normal there stays wound
  // counter-clockwise about the mapped normal here and the indices pass
  // through in order. Walls now carry both-side metadata and select their
  // normal/material in the shader. This helper also serves horizontal detail,
  // where an explicitly mirrored surface still flips normal and winding.
  auto sign = mirrored ? -1.0f : 1.0f;
  uint32_t indices[3];
  for (int i = 0; i < 3; ++i) {
    auto const& vertex = triangle.v[i];
    indices[i] = addVertexToDataProvider(
        dataProvider, meshIndex, vertex.position[0], vertex.position[2],
        -vertex.position[1], sign * vertex.normal[0], sign * vertex.normal[2],
        -sign * vertex.normal[1], vertex.uv[0], vertex.uv[1], colour,
        liquidSurfaceHeight, surfaceUpX, surfaceUpY, surfaceUpZ);
  }
  if (mirrored) {
    dataProvider->addTriangle(meshIndex, indices[2], indices[1], indices[0]);
  } else {
    dataProvider->addTriangle(meshIndex, indices[0], indices[1], indices[2]);
  }
}

void WorldRenderer::updateHorizontalDataProvider(
    bw::core::WorldData const& snapshot,
    int32_t highlightedTriangle,
    bool highlightedCeiling,
    DataProvider const& dataProvider) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getTriangles();
  auto const highlightedFace =
      highlightedTriangle >= 0 && size_t(highlightedTriangle) < triangles.size()
          ? triangles[highlightedTriangle].face
          : ~0u;
  auto horizontal = mMaterialRenderers[0];
  horizontal.dataProvider = dataProvider;
  // A Chip bites into one side of a face only, so the detail channel names
  // the side (ADR-0027): skipping the whole ArrangementTriangle would punch
  // a matching hole in the surface overhead.
  auto const& detail = snapshot.getDetail();
  using bw::core::arr::DetailSurfaceKind;
  auto const& poolElevations = snapshot.getLiquidPoolElevations();
  auto const& hydraulicCells = snapshot.getHydraulicCells();
  auto liquidSurfaceHeightForCell = [&](size_t cellIndex) {
    auto elevation = poolElevations[cellIndex];
    return std::isfinite(elevation) &&
                   hydraulicCells[cellIndex].volumeBelow(elevation) > 0.0
               ? float(elevation)
               : WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
  };
  auto liquidSurfaceHeightForDetail =
      [&](bw::core::arr::DetailTriangle const& detailTriangle) {
        if (detailTriangle.source.kind == DetailSurfaceKind::Wall) {
          return WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
        }
        wp::Vector2 centroid{};
        for (auto const& vertex : detailTriangle.v) {
          centroid += {vertex.position[0], vertex.position[1]};
        }
        centroid /= 3.0f;
        auto elevation = snapshot.getLiquidSurfaceHeight(centroid);
        return std::isfinite(elevation)
                   ? elevation
                   : WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
      };

  // A rebuilt face's replacements resolve exactly the Sub-material the face
  // itself would have, so they land in mesh buckets that already exist.
  auto horizontalMeshFor = [&](bw::core::arr::DetailSurfaceKey const& key) {
    auto const& properties =
        worldData.palette[worldData.faces[key.index].paletteIndex];
    auto isFloor = key.kind == DetailSurfaceKind::FloorOfFace;
    auto resolved = mBakedSurfaceMaterialResolver.resolve(
        isFloor ? properties.floorMaterial : properties.ceilingMaterial,
        isFloor ? properties.floorEmbossPresetId
                : properties.ceilingEmbossPresetId);
    return horizontal.renderer->getMeshIndexForMaterialHash(
        resolved.hash(), isFloor);
  };
  auto surfaceUpFor = [&](bw::core::arr::DetailSurfaceKey const& key) {
    auto const& properties =
        worldData.palette[worldData.faces[key.index].paletteIndex];
    auto up = key.kind == DetailSurfaceKind::FloorOfFace
                  ? properties.floorZ.normal()
                  : properties.ceilingZ.normal();
    return std::array<float, 3>{up[0], up[2], -up[1]};
  };

  std::vector<uint32_t> horizontalCounts(
      horizontal.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    auto const& properties = worldData.palette[worldData.faces[triangle.face].paletteIndex];
    auto floorResolved = mBakedSurfaceMaterialResolver.resolve(
        properties.floorMaterial, properties.floorEmbossPresetId);
    auto floorHash = floorResolved.hash();
    auto ceilingResolved = mBakedSurfaceMaterialResolver.resolve(
        properties.ceilingMaterial, properties.ceilingEmbossPresetId);
    auto ceilingHash = ceilingResolved.hash();
    if (!detail.isSuppressed(DetailSurfaceKind::FloorOfFace, triangle.face)) {
      ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
          floorHash, true)];
    }
    if (!detail.isSuppressed(
            DetailSurfaceKind::CeilingOfFace, triangle.face)) {
      ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
          ceilingHash, false)];
    }
  }
  for (auto const& replacement : detail.getTriangles()) {
    if (replacement.source.kind == DetailSurfaceKind::Wall) {
      continue;
    }
    ++horizontalCounts[horizontalMeshFor(replacement.source)];
  }
  horizontal.dataProvider->updateInternals(horizontalCounts);

  for (size_t triangleIndex = 0; triangleIndex < triangles.size();
       ++triangleIndex) {
    auto const& triangle = triangles[triangleIndex];
    auto const& face = worldData.faces[triangle.face];
    auto const& properties = worldData.palette[face.paletteIndex];
    wp::Vector2 positions[3];
    for (int i = 0; i < 3; ++i) {
      auto const& vertex = worldData.vertices[triangle.v[i]];
      positions[i] = {
          bw::core::arr::ToWorldCoordinate(vertex.x),
          bw::core::arr::ToWorldCoordinate(vertex.y)};
    }

    auto liquidSurfaceHeight = liquidSurfaceHeightForCell(triangleIndex);

    if (!detail.isSuppressed(DetailSurfaceKind::FloorOfFace, triangle.face)) {
      auto floorResolved = mBakedSurfaceMaterialResolver.resolve(
          properties.floorMaterial, properties.floorEmbossPresetId);
      auto floorHash = floorResolved.hash();
      auto floorMesh = horizontal.renderer->getMeshIndexForMaterialHash(
          floorHash, true);
      uint32_t floorIndices[3];
      for (int i = 0; i < 3; ++i) {
        auto uv = positions[i] / 64.0f;
        // Reflecting authored Y into renderer -Z reverses winding, so reverse
        // the indices as well to preserve the floor's front face.
        floorIndices[2 - i] = addVertexToDataProvider(
            horizontal.dataProvider, floorMesh, positions[i].x,
            triangle.floor.elevation[i], -positions[i].y,
            triangle.floor.normal[0], triangle.floor.normal[2],
            -triangle.floor.normal[1], uv.x, uv.y,
            triangle.face == highlightedFace && !highlightedCeiling
                ? lookedAtVertexColour
                : untintedVertexColour,
            liquidSurfaceHeight, triangle.floor.normal[0],
            triangle.floor.normal[2], -triangle.floor.normal[1]);
      }
      horizontal.dataProvider->addTriangle(
          floorMesh, floorIndices[0], floorIndices[1], floorIndices[2]);
    }

    if (detail.isSuppressed(DetailSurfaceKind::CeilingOfFace, triangle.face)) {
      continue;
    }
    auto ceilingResolved = mBakedSurfaceMaterialResolver.resolve(
        properties.ceilingMaterial, properties.ceilingEmbossPresetId);
    auto ceilingHash = ceilingResolved.hash();
    auto ceilingMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false);
    uint32_t ceilingIndices[3];
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      ceilingIndices[i] = addVertexToDataProvider(
          horizontal.dataProvider, ceilingMesh, positions[i].x,
          triangle.ceiling.elevation[i], -positions[i].y,
          triangle.ceiling.normal[0], triangle.ceiling.normal[2],
          -triangle.ceiling.normal[1], uv.x, uv.y,
          triangle.face == highlightedFace && highlightedCeiling
              ? lookedAtVertexColour
              : untintedVertexColour,
          liquidSurfaceHeight, -triangle.ceiling.normal[0],
          -triangle.ceiling.normal[2], triangle.ceiling.normal[1]);
    }
    horizontal.dataProvider->addTriangle(
        ceilingMesh, ceilingIndices[0], ceilingIndices[1], ceilingIndices[2]);
  }

  for (auto const& replacement : detail.getTriangles()) {
    if (replacement.source.kind == DetailSurfaceKind::Wall) {
      continue;
    }
    auto highlighted = replacement.source.index == highlightedFace &&
                       highlightedCeiling ==
                           (replacement.source.kind == DetailSurfaceKind::CeilingOfFace);
    auto surfaceUp = surfaceUpFor(replacement.source);
    addDetailTriangleToDataProvider(
        horizontal.dataProvider, horizontalMeshFor(replacement.source),
        replacement, false,
        highlighted ? lookedAtVertexColour : untintedVertexColour,
        liquidSurfaceHeightForDetail(replacement), surfaceUp[0], surfaceUp[1],
        surfaceUp[2]);
  }
  horizontal.dataProvider->finalizeInternals();
  horizontal.dataProvider->setNumPrimitives(horizontal.dataProvider->getNumTriangles());
}

void WorldRenderer::updateLiquidDataProvider(
    bw::core::WorldData const& snapshot,
    DataProvider const& dataProvider) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getLiquidSurfaceTriangles();
  auto liquid = mMaterialRenderers[1];
  liquid.dataProvider = dataProvider;
  auto liquidHashFor = [](bw::core::LiquidType liquidType) {
    return bw::core::MaterialDefinition{}.data.hash(
        bw::core::LiquidMaterialIndex(liquidType));
  };

  std::vector<uint32_t> counts(liquid.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    auto const& properties =
        worldData.palette[worldData.faces[triangle.face].paletteIndex];
    ++counts[liquid.renderer->getMeshIndexForMaterialHash(
        liquidHashFor(properties.liquidType), true)];
  }
  liquid.dataProvider->updateInternals(counts);

  for (auto const& triangle : triangles) {
    auto const& properties =
        worldData.palette[worldData.faces[triangle.face].paletteIndex];
    auto mesh = liquid.renderer->getMeshIndexForMaterialHash(
        liquidHashFor(properties.liquidType), true);
    uint32_t indices[3];
    for (int i = 0; i < 3; ++i) {
      auto const& position = triangle.positions[i];
      auto uv = position / 64.0f;
      // Reflecting authored Y into renderer -Z reverses winding.
      indices[2 - i] = addVertexToDataProvider(
          liquid.dataProvider, mesh, position.x, triangle.elevation,
          -position.y, 0, 1, 0, uv.x, uv.y, transparentVertexColour,
          triangle.elevation);
    }
    liquid.dataProvider->addTriangle(mesh, indices[0], indices[1], indices[2]);
  }
  liquid.dataProvider->finalizeInternals();
  liquid.dataProvider->setNumPrimitives(liquid.dataProvider->getNumTriangles());
}

void WorldRenderer::updateWallDataProvider(
    bw::core::WorldData const& snapshot, DataProvider const& dataProvider) {
  auto const& arrangement = snapshot.getArrangement();
  auto const& walls = snapshot.getWalls();
  auto const& detail = snapshot.getDetail();
  using bw::core::arr::DetailSurfaceKind;
  using DrawVert = WorldTriangle3dDataProvider::DrawVert;
  if (walls.size() > 16777215u) {
    throw std::runtime_error("wall IDs exceed the exact float vertex encoding");
  }
  auto projectionData = BuildTriplanarWallProjectionData(arrangement, walls);
  auto const& renderer = mMaterialRenderers[2].renderer;
  auto backHash = bw::core::MaterialDefinition{}.data.hash(
      BW_WALL_BACK_FACE_MATERIAL_INDEX);
  auto backMesh = renderer->getMeshIndexForMaterialHash(backHash, false);

  // Build only at snapshot publication. Staging triangles lets us calculate
  // exact per-bucket capacities without evaluating geometry/materials twice.
  std::vector<std::vector<DrawVert>> meshes(dataProvider->getNumMeshes());
  for (uint32_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) continue;
    auto orientation = bw::core::arr::OrientArrangementWall(arrangement, wall);
    auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
    auto liquidHeight = [&](float side) {
      auto height = snapshot.getLiquidSurfaceHeight(
          midpoint + orientation.normal * (side * 0.01f));
      return std::isfinite(height) ? height
          : WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
    };
    auto frontLiquid = liquidHeight(1.0f);
    auto backLiquid = liquidHeight(-1.0f);
    auto const& properties = arrangement.palette[wall.paletteIndex];
    auto resolved = mBakedSurfaceMaterialResolver.resolve(
        properties.wallMaterial, properties.wallEmbossPresetId);
    auto hash = resolved.hash();
    auto variant = mWallRenderVariantResolver
        ? mWallRenderVariantResolver(wall) : optional<WallRenderVariant>{};
    auto mesh = renderer->getMeshIndexForMaterialHash(hash, false, variant);
    auto unmappedMesh = renderer->getMeshIndexForMaterialHash(hash, false);
    auto vertex = [&](float x, float y, float z, float nx, float ny, float nz,
                      float u, float v, bool followsWall,
                      std::array<float, 3> projection) -> DrawVert {
      return {{x, y, z}, {nx, ny, nz},
              {u, v, float(wallIndex + 1) * (followsWall ? 1.0f : -1.0f), backLiquid},
              untintedVertexColour, {projection[0], projection[1], projection[2]}, frontLiquid};
    };
    std::array<float, 3> normal{orientation.normal.x, 0.0f, -orientation.normal.y};
    if (detail.isSuppressed(DetailSurfaceKind::Wall, wallIndex)) {
      for (auto const& original : detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex)) {
        auto triangle = original;
        ApplyWallPhysicalUvToRemainder(orientation, wall, triangle);
        auto replacementMesh = triangle.kind == bw::core::arr::DetailTriangleKind::SurfaceRemainder
            ? mesh : unmappedMesh;
        if (triangle.kind == bw::core::arr::DetailTriangleKind::PortalFallback) {
          replacementMesh = backMesh;
          for (auto const& [key, bucket] : mPortalEndpointBuckets) {
            auto const* pair = snapshot.findPortalPair(key.layerId, key.pairId);
            if (!pair || !pair->active) continue;
            auto endpoint = std::ranges::find_if(pair->endpoints, [&](auto const& item) {
              return item.endpointId == key.endpointId;
            });
            if (endpoint == pair->endpoints.end() ||
                std::ranges::find(endpoint->aperture.wallIndices, wallIndex) == endpoint->aperture.wallIndices.end() ||
                !portalFallbackBelongsTo(triangle, endpoint->aperture)) continue;
            replacementMesh = renderer->getMeshIndexForMaterialHash(hash, false,
                WallRenderVariant{.identity = portalWallRenderVariantIdentity(bucket)});
            break;
          }
        }
        for (auto const& v : triangle.v) {
          meshes[replacementMesh].push_back(vertex(
              v.position[0], v.position[2], -v.position[1],
              v.normal[0], v.normal[2], -v.normal[1], v.uv[0], v.uv[1],
              triangle.followsWallFacing, normal));
        }
      }
    } else if (projectionData[wallIndex].usesTriplanar) {
      for (auto const& triangle : BuildTriplanarWallRenderTriangles(
               arrangement, wall, projectionData[wallIndex])) {
        for (auto const& v : triangle.vertices) {
          meshes[mesh].push_back(vertex(v.position.x, v.elevation, -v.position.y,
              normal[0], normal[1], normal[2], v.u, v.v, true,
              {v.projectionNormal.x, 0.0f, -v.projectionNormal.y}));
        }
      }
    } else {
      auto uv = CalculateWallPhysicalUv(orientation, wall);
      auto surface = bw::core::arr::BuildArrangementWallSurface(arrangement, wall);
      auto surfaceVertex = [&](uint8_t corner) {
        auto const& v = surface.vertices[corner];
        return vertex(v.position.x, v.elevation, -v.position.y,
            normal[0], normal[1], normal[2], v.endpoint == 0 ? uv.u0 : uv.u1,
            v.topBoundary ? uv.topV[v.endpoint] : uv.bottomV[v.endpoint], true, normal);
      };
      for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
        meshes[mesh].push_back(surfaceVertex(0));
        meshes[mesh].push_back(surfaceVertex(corner));
        meshes[mesh].push_back(surfaceVertex(corner + 1));
      }
    }
  }
  std::vector<uint32_t> counts;
  for (auto const& mesh : meshes) counts.push_back(uint32_t(mesh.size() / 3));
  dataProvider->updateInternals(counts);
  for (uint32_t mesh = 0; mesh < meshes.size(); ++mesh) {
    for (size_t first = 0; first < meshes[mesh].size(); first += 3) {
      auto a = dataProvider->addVertex(mesh, meshes[mesh][first]);
      auto b = dataProvider->addVertex(mesh, meshes[mesh][first + 1]);
      auto c = dataProvider->addVertex(mesh, meshes[mesh][first + 2]);
      dataProvider->addTriangle(mesh, a, b, c);
    }
  }
  dataProvider->finalizeInternals();
  dataProvider->setNumPrimitives(dataProvider->getNumTriangles());
}

void WorldRenderer::renderScene(
    bw::core::WorldData const& worldData, mpp::CameraPtr const& camera,
    mpp::RenderPipelinePtr const& pipeline, uint32_t width, uint32_t height) {
  if (mZone == bw::core::ZoneId::Phantom)
    renderPhantomScene(worldData, camera, pipeline, width, height);
  else
    renderWorldScene(worldData, camera, pipeline, width, height);
}

void WorldRenderer::renderPhantomScene(
    bw::core::WorldData const& worldData, mpp::CameraPtr const& camera,
    mpp::RenderPipelinePtr const& pipeline, uint32_t width, uint32_t height) {
  if (!mScene || !mRenderSystem || !camera || !pipeline || !width || !height)
    throw std::invalid_argument("Phantom rendering requires a created scene, camera and target");
  auto const& arrangement = worldData.getArrangement();
  auto const& walls = worldData.getWalls();
  // Snapshot changes, not camera/Zone changes, own the aperture geometry.
  if (!mPhantom || mPhantomGeometryDirty) {
    mPhantom.reset();
    mPhantom = std::make_unique<PhantomRenderState>();
    auto& state = *mPhantom;
    state.system = mRenderSystem;
    state.scene = std::make_shared<PhantomRenderState::ApertureScene>(mRenderSystem, mScene);
    state.scene->setClearColour({0.0f, 0.0f, 0.0f, 1.0f});
    std::vector<WallRenderSurface> surfaces;
    for (uint32_t index = 0; index < walls.size(); ++index) {
      if (!bw::core::isPhantomAperture(walls[index])) continue;
      state.walls.push_back(index);
      auto const& properties = arrangement.palette[walls[index].paletteIndex];
      surfaces.push_back({properties.wallMaterial,
          WallRenderVariant{.identity = portalWallRenderVariantIdentity(index)},
          properties.wallEmbossPresetId});
    }
    state.provider = std::make_shared<WorldTriangle3dDataProvider>();
    state.renderer = std::make_shared<WorldRenderer3d>(
        mResourceMgr->getResource("Material.Default", "World"),
        mResourceMgr->getResource("Material.FragmentOverdraw", "World"), mwLogger,
        WorldSurfaceSet::Walls, &mSurfaceMaterialResolver, surfaces, false,
        mBatchNamePrefix + ".Phantom", true);
    state.renderer->create(state.provider, mwWorld, mRenderSystem, mRenderResourceMgr);
    state.renderer->addToScene(state.scene, mwWorld);
    std::vector<uint32_t> counts(state.provider->getNumMeshes());
    auto meshFor = [&](uint32_t index) {
      auto const& properties = arrangement.palette[walls[index].paletteIndex];
      auto resolved = mSurfaceMaterialResolver.resolve(properties.wallMaterial, properties.wallEmbossPresetId);
      return state.renderer->getMeshIndexForMaterialHash(resolved.hash(), false,
          WallRenderVariant{.identity = portalWallRenderVariantIdentity(index)});
    };
    for (auto index : state.walls) {
      auto surface = bw::core::arr::BuildArrangementWallSurface(arrangement, walls[index]);
      if (surface.vertexCount >= 3) counts[meshFor(index)] += surface.vertexCount - 2;
    }
    state.provider->updateInternals(counts);
    for (auto index : state.walls) {
      auto mesh = meshFor(index);
      auto frame = bw::core::arr::OrientArrangementWall(arrangement, walls[index]);
      auto surface = bw::core::arr::BuildArrangementWallSurface(arrangement, walls[index]);
      auto vertex = [&](uint8_t corner) {
        auto const& v = surface.vertices[corner];
        return addVertexToDataProvider(state.provider, mesh,
            v.position.x, v.elevation, -v.position.y,
            frame.normal.x, 0.0f, -frame.normal.y, 0, 0, untintedVertexColour);
      };
      for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
        auto a = vertex(0), b = vertex(corner), c = vertex(corner + 1);
        state.provider->addTriangle(mesh, a, b, c);
      }
    }
    state.provider->finalizeInternals();
    state.provider->setNumPrimitives(state.provider->getNumTriangles());
    mPhantomGeometryDirty = false;
  }
  auto& state = *mPhantom;
  if (state.hostPipeline.lock() != pipeline) {
    state.clearPipelines();
    state.hostPipeline = pipeline;
  }
  state.scene->setViewport(0, 0, width, height);
  state.renderer->setPortalFallback();
  state.renderer->update(camera->getPosition(), mPlayerTorchPosition,
      WorldTriangle3dDataProvider::dryLiquidSurfaceHeight, {}, {}, {}, {},
      defaultLiquidReflectionMipLevel, false, mPlayerTorchOptions, false,
      -1, 32.0f, 1.0f / 32.0f, {}, 0.0f);
  auto view = camera->getViewTransform();
  auto projection = camera->getProjectionTransform();
  auto eye = camera->getPosition();
  std::set<uint32_t> usedViews;
  mLastPortalViewPlan = {};
  mSelectedPortal.reset();
  for (auto index : state.walls) {
    if (!bw::core::phantomApertureFacesEye(arrangement, walls[index], {eye.x, -eye.z})) continue;
    auto surface = bw::core::arr::BuildArrangementWallSurface(arrangement, walls[index]);
    std::array<uint8_t, 5> outside{};
    for (uint8_t corner = 0; corner < surface.vertexCount; ++corner) {
      auto const& vertex = surface.vertices[corner];
      auto p = projection * view * glm::vec4(vertex.position.x, vertex.elevation, -vertex.position.y, 1.0f);
      outside[0] += p.x < -p.w; outside[1] += p.x > p.w;
      outside[2] += p.y < -p.w; outside[3] += p.y > p.w;
      outside[4] += p.z > p.w;
    }
    // Do not reject the near plane: the aperture is depth-clamped until the
    // centre crosses it, just like an ordinary Portal surface.
    if (surface.vertexCount < 3 || std::ranges::any_of(outside,
          [&](auto count) { return count == surface.vertexCount; })) continue;
    usedViews.insert(index);
    auto frame = bw::core::arr::OrientArrangementWall(arrangement, walls[index]);
    glm::vec3 normal{frame.normal.x, 0.0f, -frame.normal.y};
    glm::vec3 point{frame.v0.x, 0.0f, -frame.v0.y};
    glm::vec4 plane{normal, -glm::dot(normal, point)};
    auto clipped = mpp::buildObliquelyClippedVirtualCamera(view, projection, plane, 0.0f);
    auto childCamera = std::make_shared<mpp::VirtualCamera>(clipped.view, clipped.projection,
        camera->getNearClipDistance(), camera->getFarClipDistance());
    auto name = mBatchNamePrefix + ".Phantom.View." + std::to_string(index);
    auto child = mRenderSystem->getOrCreateRenderPipeline(name, pipeline->getOptions());
    state.pipelines[index] = child;
    child->setBloomOptions(pipeline->getOptions().bloom);
    child->setAmbientOcclusionOptions(pipeline->getOptions().ambientOcclusion);
    child->setGraphPassDebugOptions(pipeline->getOptions().graphPasses);
    child->resize(width, height);
    // renderWorldScene deliberately bypasses Phantom dispatch. All nested
    // Portal and reflection cameras inherit Euclidean material treatment.
    renderWorldScene(worldData, childCamera, child, width, height);
    auto const& outputs = pipeline->getOptions().outputs;
    auto texture = std::dynamic_pointer_cast<mpp::RenderTexture>(outputs.empty()
        ? child->getOutputRenderTarget() : child->getOutputRenderTarget(outputs.front().name));
    if (!texture) throw std::runtime_error("Phantom view has no sampleable output");
    state.renderer->setPortalView(index, std::static_pointer_cast<mpp::Resource>(texture),
        projection * view, true);
  }
  for (auto it = state.pipelines.begin(); it != state.pipelines.end();) {
    if (!usedViews.contains(it->first)) {
      mRenderSystem->removeRenderPipeline(it->second->getName());
      it = state.pipelines.erase(it);
    } else ++it;
  }
  // Apertures write their own depth, so the nearest one owns overlapping
  // pixels even when its child image contains only black. No primary world
  // geometry, liquid or physical-world markers can leak outside their outline.
  mRenderSystem->renderScene(state.scene, camera, {0.0f, 0.0f}, pipeline->getName());
}

void WorldRenderer::renderWorldScene(
    bw::core::WorldData const& worldData,
    mpp::CameraPtr const& camera,
    mpp::RenderPipelinePtr const& pipeline,
    uint32_t width,
    uint32_t height) {
  if (!mScene || !mRenderSystem || !camera || !pipeline) {
    throw std::invalid_argument(
        "Portal-capable World rendering requires a created scene, camera, and pipeline");
  }

  mLastPortalViewPlan = mPortalViewPlanner.build(
      worldData.getPortalPairs(), camera->getViewTransform(),
      camera->getProjectionTransform(), camera->getNearClipDistance(),
      camera->getFarClipDistance(), width, height);
  mSelectedPortal = mLastPortalViewPlan.rootChildren.empty()
                        ? std::nullopt
                        : std::optional<PortalEndpointKey>{
                              mLastPortalViewPlan.rootChildren.front().endpoint};

  auto& walls = mMaterialRenderers[2].renderer;
  std::vector<mpp::ResourcePtr> renderedTextures(
      mLastPortalViewPlan.nodes.size());

  // Portal lighting is a property of the rendered World position, not of the
  // camera observing it. The same deterministically bounded path set is
  // attached to the primary view and every Auxiliary view.
  mLastPortalLightPlan = PlanPortalLights(
      worldData.getPortalPairs(), mPlayerTorchPosition,
      mPlayerTorchOptions, mPortalLightLimits);

  std::vector<PortalLightShadowAttachment> portalLights;
  auto reportShadowFailure = [&](std::string const& reason) {
    if (mwLogger) {
      mwLogger->warn(
          "Portal-transmitted Player Torch path disabled: " + reason);
    }
  };
  auto markShadowUnavailable = [&](PortalLightAttachment const& light) {
    auto diagnostic = std::ranges::find_if(
        mLastPortalLightPlan.diagnostics, [&](auto const& item) {
          return item.reason == PortalLightDiagnosticReason::Retained &&
                 item.path == light.path;
        });
    if (diagnostic != mLastPortalLightPlan.diagnostics.end()) {
      diagnostic->reason = PortalLightDiagnosticReason::ShadowUnavailable;
    }
  };
  auto const& ordinaryDomain = pipeline->getOptions().shadowDomain;
  auto shadowsAvailable = !ordinaryDomain.empty() &&
                          mRenderSystem->hasShadowDomain(ordinaryDomain);
  std::optional<mpp::ShadowOptions> ordinaryOptions;
  if (shadowsAvailable) {
    ordinaryOptions = mRenderSystem->getShadowDomainOptions(ordinaryDomain);
    shadowsAvailable = ordinaryOptions->enabled &&
                       ordinaryOptions->light.type ==
                           mpp::ShadowLightType::Point;
  }
  if (!mLastPortalLightPlan.lights.empty() && !shadowsAvailable) {
    reportShadowFailure(
        "ordinary Player Torch point-shadow domain is unavailable");
    for (auto const& light : mLastPortalLightPlan.lights) {
      markShadowUnavailable(light);
    }
  } else if (ordinaryOptions) {
    auto pointCasterClip = [](glm::vec3 const& centre,
                              glm::vec3 const& tangent,
                              glm::vec3 const& front,
                              float halfWidth,
                              float bottom,
                              float top) {
      mpp::PointShadowCasterClip clip;
      clip.enabled = true;
      auto normal = glm::normalize(front);
      clip.retainedWorldPlane =
          glm::vec4(normal, -glm::dot(normal, centre));
      clip.openingCentre = {
          centre.x, (bottom + top) * 0.5f, centre.z};
      clip.openingTangent = glm::normalize(tangent);
      clip.openingBitangent = {0.0f, 1.0f, 0.0f};
      clip.openingHalfSize = {
          halfWidth, (top - bottom) * 0.5f};
      clip.planeTolerance = 0.01f;
      return clip;
    };
    auto renderLeg = [&](std::string const& domain,
                         mpp::ShadowOptions const& options) {
      mRenderSystem->configureShadowDomain(domain, options);
      auto target = mRenderSystem->getShadowDomainDepthTarget(domain);
      if (!target ||
          !mRenderSystem->getShadowDomainOptions(domain).enabled) {
        throw std::runtime_error(domain + " cubemap allocation failed");
      }
      auto casters = mScene->get3dModelsInSphere(
          options.light.position, options.light.range);
      mRenderSystem->renderShadowDomain(domain, casters);
      auto diagnostics = mRenderSystem->getShadowDomainDiagnostics(domain);
      if (!diagnostics.cacheComplete) {
        throw std::runtime_error(domain + " cubemap execution was incomplete");
      }
      auto resource = std::dynamic_pointer_cast<mpp::Resource>(target);
      if (!resource) {
        throw std::runtime_error(
            domain + " point-shadow target is not sampleable");
      }
      return resource;
    };

    for (size_t lightIndex = 0;
         lightIndex < mLastPortalLightPlan.lights.size(); ++lightIndex) {
      auto const& light = mLastPortalLightPlan.lights[lightIndex];
      try {
        std::vector<mpp::ResourcePtr> maps;
        maps.reserve(light.hops.size() + 1);
        for (size_t leg = 0; leg <= light.hops.size(); ++leg) {
          auto options = *ordinaryOptions;
          if (leg == 0) {
            auto const& first = light.hops.front();
            options.light.position = light.sourcePosition;
            options.pointCasterClip = pointCasterClip(
                first.sourceApertureCentre,
                first.sourceApertureTangent,
                first.sourceApertureFront,
                first.apertureHalfWidth,
                first.sourceApertureBottom,
                first.sourceApertureTop);
          } else {
            auto const& previous = light.hops[leg - 1];
            options.light.position = previous.virtualLightPosition;
            options.pointCasterClip = pointCasterClip(
                previous.destinationApertureCentre,
                previous.destinationApertureTangent,
                previous.destinationApertureFront,
                previous.apertureHalfWidth,
                previous.destinationApertureBottom,
                previous.destinationApertureTop);
          }

          std::string domain;
          if (mLastPortalLightPlan.lights.size() == 1 &&
              light.hops.size() == 1) {
            domain = leg == 0
                         ? "BooleanWorld.PortalTorch.SourceLeg"
                         : "BooleanWorld.PortalTorch.DestinationLeg";
          } else {
            domain = "BooleanWorld.PortalTorch.Path" +
                     std::to_string(lightIndex) + ".Leg" +
                     std::to_string(leg);
          }
          maps.push_back(renderLeg(domain, options));
        }
        portalLights.push_back({light,
                                std::move(maps),
                                ordinaryOptions->light.range,
                                ordinaryOptions->constantBias,
                                ordinaryOptions->normalBias,
                                ordinaryOptions->filterRadiusTexels,
                                ordinaryOptions->fadeStartNormalized,
                                1.0f / static_cast<float>(ordinaryOptions->resolution),
                                ordinaryOptions->filterMode == mpp::ShadowFilterMode::Pcf3x3});
      } catch (std::exception const& error) {
        markShadowUnavailable(light);
        reportShadowFailure(error.what());
      }
    }
  }

  // Every pass shares the published buffers and the player's Zone material
  // uniforms. renderAuxiliaryScene supplies each transformed CameraFrame, so
  // shaders classify geometric facing from that eye, not PLAYER_POSITION.
  // Portal placement/traversal never selects a Zone or rebuilds endpoint buckets.
  // Only endpoint texture bindings change; a child target must have completed
  // earlier in deepest-first order.
  auto configurePortalSurfaces = [&](std::vector<PortalViewPlanEdge> const& edges, bool clampNearPlane) {
    walls->setPortalFallback();
    for (auto const& edge : edges) {
      auto const& texture = renderedTextures[edge.childNode];
      if (!texture) {
        throw std::logic_error(
            "Portal view dependency was not rendered deepest-first");
      }
      walls->setPortalView(
          mPortalEndpointBuckets.at(edge.endpoint), texture, edge.sourceProjectiveTransform, clampNearPlane);
    }
  };

  for (auto nodeIndex : mLastPortalViewPlan.deepestFirst) {
    auto const& node = mLastPortalViewPlan.nodes[nodeIndex];
    configurePortalSurfaces(node.children, false);
    auto auxiliary = node.auxiliary;
    if (!AttachPortalLightsToPass(auxiliary, portalLights)) {
      reportShadowFailure(
          "Auxiliary pass rejected incomplete or conflicting shadow state");
    }
    auto outputs = mRenderSystem->renderAuxiliaryScene(
        mScene, camera, pipeline->getName(), auxiliary);
    auto renderTexture =
        std::dynamic_pointer_cast<mpp::RenderTexture>(outputs.colour);
    if (!renderTexture) {
      throw std::runtime_error("Portal auxiliary colour output is not a texture");
    }
    renderedTextures[nodeIndex] =
        std::static_pointer_cast<mpp::Resource>(renderTexture);
  }

  configurePortalSurfaces(mLastPortalViewPlan.rootChildren, true);
  mpp::ScenePassOverrides primaryPass;
  if (!AttachPortalLightsToPass(primaryPass, portalLights)) {
    reportShadowFailure(
        "primary pass rejected incomplete or conflicting shadow state");
  }
  mRenderSystem->renderScene(
      mScene, camera, {0.0f, 0.0f}, pipeline->getName(), primaryPass);
}

std::optional<PortalEndpointKey> const&
WorldRenderer::getSelectedPortal() const {
  return mSelectedPortal;
}

PortalViewPlan const& WorldRenderer::getPortalViewDiagnostics() const {
  return mLastPortalViewPlan;
}

PortalLightPlan const& WorldRenderer::getPortalLightDiagnostics() const {
  return mLastPortalLightPlan;
}

void WorldRenderer::setPortalLightLimits(PortalLightLimits limits) {
  mPortalLightLimits.maxHops =
      std::min(limits.maxHops, PortalLightHopLimit);
  mPortalLightLimits.maxVirtualLights =
      std::min(limits.maxVirtualLights, PortalLightAttachmentLimit);
  mPortalLightLimits.maxShadowPasses =
      std::min(limits.maxShadowPasses, PortalLightShadowPassLimit);
}

void WorldRenderer::update(
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
    int32_t highlightedTriangle,
    bool highlightedCeiling,
    int32_t highlightedWall) {
  BW_UNUSED(world);

  mPlayerTorchPosition = lightPosition;
  mPlayerTorchOptions = playerTorch;

  auto const horizontalHighlightChanged =
      highlightedTriangle != mHighlightedTriangle ||
      highlightedCeiling != mHighlightedCeiling;
  mHighlightedTriangle = highlightedTriangle;
  mHighlightedCeiling = highlightedCeiling;

  auto const worldHasChanged = mWorldHasChanged;
  if (worldHasChanged || horizontalHighlightChanged) {
    updateHorizontalDataProvider(
        worldData, highlightedTriangle, highlightedCeiling,
        mMaterialRenderers[0].dataProvider);
  }
  if (worldHasChanged) {
    updateLiquidDataProvider(
        worldData, mMaterialRenderers[1].dataProvider);
  }
  if (worldHasChanged) {
    updateWallDataProvider(worldData, mMaterialRenderers[2].dataProvider);
  }
  mMaterialRenderers[2].renderer->setHighlightedWall(highlightedWall);
  mWorldHasChanged = false;

  auto liquidEyeSurfaceHeight =
      worldData.getLiquidSurfaceHeight({playerPosition.x, -playerPosition.z});
  // A dry position resolves to Water, which is also the only currently
  // authored LiquidType. Keep its optical properties available so a dry eye
  // can absorb a wet far endpoint; the dry surface sentinel still makes every
  // dry-to-dry path exactly zero.
  // F5's session-only overrides stand in for authored optical tuning alone;
  // density, viscosity, and referenceDepth stay as authored.
  auto liquid = bw::core::GetLiquidProperties(
      worldData.getLiquidType({playerPosition.x, -playerPosition.z}));
  if (liquidOpacityOverride) {
    liquid.opacity = *liquidOpacityOverride;
  }
  if (liquidTintOverride) {
    liquid.tint = *liquidTintOverride;
  }
  auto const extinction = bw::core::CalculateLiquidExtinction(liquid);
  glm::vec3 liquidExtinction{
      extinction[0], extinction[1], extinction[2]};
  glm::vec3 liquidTint{liquid.tint[0], liquid.tint[1], liquid.tint[2]};
  if (mZone == bw::core::ZoneId::Phantom || !std::isfinite(liquidEyeSurfaceHeight)) {
    liquidEyeSurfaceHeight =
        WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
  }

  for (auto& item : mMaterialRenderers) {
    auto const materialIndexOverride =
        item.surfaceSet == WorldSurfaceSet::Walls
            ? wallMaterialIndexOverride
            : horizontalMaterialIndexOverride;
    item.renderer->update(
        playerPosition, lightPosition, liquidEyeSurfaceHeight, liquidExtinction,
        liquidTint, liquidReflectanceOverride, liquidF0Override,
        liquidReflectionMipLevel,
        liquidReflectionEnabled, playerTorch, sortGeometryFrontToBack,
        materialIndexOverride, materialScale,
        pixelSize, secondaryMaterial, frameTime);
  }
}