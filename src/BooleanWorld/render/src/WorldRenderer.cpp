#include <bit>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

#include <common/GameDefines.h>

#include <core/Defines.h>
#include <core/LiquidProperties.h>
#include <core/LiquidType.h>
#include <core/MaterialDefinition.h>
#include <core/World.h>

#include <willpower/application/resourcesystem/ImageResource.h>

#include "WorldRenderer.h"
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

struct MaskPayload {
  mpp::ResourcePtr texture;
  uint8_t channel{0};
  bw::core::WallMaskOverride::BlendParameters blendParameters{};
  bw::core::WallMaskOverride::BlendColour blendColour{1.0f, 1.0f, 1.0f};
  float aspectRatio{1.0f};
};

}  // namespace

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

void WorldRenderer::create(mpp::ScenePtr scene, bw::core::World* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  mwWorld = world;

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
  for (auto const& [material, embossPresetId] : wallSurfaceMaterials) {
    for (auto const& [identity, variant] : mWallImageVariants) {
      BW_UNUSED(identity);
      mWallRenderSurfaces.push_back(
          {material, variant, embossPresetId});
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
      {px, py, pz}, {nx, ny, nz}, {u, v}, c, {projection[0], projection[1], projection[2]}, liquidSurfaceHeight};
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
  // through in order. The world draws unculled either way (which is why each
  // wall picks one surface per frame rather than emitting both sides); what
  // carries the surface is the explicit normal, so mirroring a wall remainder
  // for its back face has to flip that as well as the winding. Chip facets are
  // never passed here as mirrored surfaces.
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
    bool highlightedCeiling) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getTriangles();
  auto const highlightedFace =
      highlightedTriangle >= 0 && size_t(highlightedTriangle) < triangles.size()
          ? triangles[highlightedTriangle].face
          : ~0u;
  auto& horizontal = mMaterialRenderers[0];
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
    bw::core::WorldData const& snapshot) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getLiquidSurfaceTriangles();
  auto& liquid = mMaterialRenderers[1];
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
    bw::core::WorldData const& snapshot, glm::vec3 const& playerPosition,
    int32_t highlightedWall) {
  auto const& worldData = snapshot.getArrangement();
  auto const& walls = snapshot.getWalls();
  auto projectionData = BuildTriplanarWallProjectionData(worldData, walls);
  auto& wallRenderer = mMaterialRenderers[2];

  // Walls render two-sided, but only ever as one triangular or quadrilateral
  // surface: whichever side currently faces the player keeps the wall's
  // authored material: this
  // reserved, plain-white material - see WorldBatch::createModelStream
  // (which guarantees this mesh bucket exists) and
  // BW_WALL_BACK_FACE_MATERIAL_INDEX - renders on the far side instead.
  // Emitting both sides' quads at once (an earlier version of this) put two
  // coplanar, oppositely-wound quads in the same mesh's material bucket,
  // which is exactly what backface culling exists to prevent overdraw of -
  // so both ended up depth-fighting for the same pixels instead of only
  // one ever being visible.
  auto backHash =
      bw::core::MaterialDefinition{}.data.hash(BW_WALL_BACK_FACE_MATERIAL_INDEX);
  wp::Vector2 playerPositionXZ{playerPosition.x, -playerPosition.z};

  auto facesPlayer = [&](bw::core::arr::ArrangementWallOrientation const& orientation) {
    auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
    return orientation.normal.dot(playerPositionXZ - midpoint) > 0.0f;
  };
  auto variantFor = [&](bw::core::arr::ArrangementWall const& wall) {
    return mWallRenderVariantResolver ? mWallRenderVariantResolver(wall)
                                      : optional<WallRenderVariant>{};
  };
  auto liquidSurfaceHeightFor =
      [&](bw::core::arr::ArrangementWallOrientation const& orientation,
          bool drawsNormalSide) {
        // This follows the per-frame facing decision: a wall can bound wet
        // and dry faces, so only the side currently being drawn contributes
        // its liquid surface to the wall vertices.
        auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
        auto position = midpoint +
                        (drawsNormalSide ? orientation.normal
                                         : -orientation.normal) *
                            0.01f;
        auto height = snapshot.getLiquidSurfaceHeight(position);
        return std::isfinite(height)
                   ? height
                   : WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
      };

  // A chipped wall draws its remainder plus the chamfer facets instead of its
  // plain surface. Only the coplanar wall remainder follows the player-facing
  // material decision above. A facet is an outward-facing surface in its own
  // right: mirroring it with the vertical wall would invert its face normal
  // when viewed from the horizontal side and make overhead lighting black.
  auto const& detail = snapshot.getDetail();
  using bw::core::arr::DetailSurfaceKind;

  std::vector<uint32_t> wallCounts(wallRenderer.dataProvider->getNumMeshes());
  for (uint32_t wallIndex = 0; wallIndex < uint32_t(walls.size());
       ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto suppressed = detail.isSuppressed(DetailSurfaceKind::Wall, wallIndex);
    auto replacements =
        suppressed
            ? detail.replacementsFor(DetailSurfaceKind::Wall, wallIndex)
            : std::span<bw::core::arr::DetailTriangle const>{};
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto resolved = mBakedSurfaceMaterialResolver.resolve(
        properties.wallMaterial, properties.wallEmbossPresetId);
    auto hash = resolved.hash();
    auto variant = variantFor(wall);
    auto authoredMesh = wallRenderer.renderer->getMeshIndexForMaterialHash(
        hash, false, variant);
    auto unmappedAuthoredMesh =
        wallRenderer.renderer->getMeshIndexForMaterialHash(hash, false);
    auto backMesh =
        wallRenderer.renderer->getMeshIndexForMaterialHash(backHash, false);
    auto orientation = bw::core::arr::OrientArrangementWall(worldData, wall);
    if (facesPlayer(orientation)) {
      if (suppressed) {
        for (auto const& replacement : replacements) {
          ++wallCounts[replacement.kind ==
                               bw::core::arr::DetailTriangleKind::SurfaceRemainder
                           ? authoredMesh
                           : unmappedAuthoredMesh];
        }
      } else if (projectionData[wallIndex].usesTriplanar) {
        wallCounts[authoredMesh] += static_cast<uint32_t>(
            BuildTriplanarWallRenderTriangles(
                worldData, wall, projectionData[wallIndex])
                .size());
      } else {
        auto surface =
            bw::core::arr::BuildArrangementWallSurface(worldData, wall);
        wallCounts[authoredMesh] += surface.vertexCount >= 3
                                        ? surface.vertexCount - 2u
                                        : 0u;
      }
    } else if (suppressed) {
      for (auto const& replacement : replacements) {
        ++wallCounts[replacement.followsWallFacing ? backMesh
                                                   : unmappedAuthoredMesh];
      }
    } else {
      auto surface =
          bw::core::arr::BuildArrangementWallSurface(worldData, wall);
      wallCounts[backMesh] += surface.vertexCount >= 3
                                  ? surface.vertexCount - 2u
                                  : 0u;
    }
  }
  wallRenderer.dataProvider->updateInternals(wallCounts);

  for (size_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto orientation = bw::core::arr::OrientArrangementWall(worldData, wall);
    auto replacements =
        detail.isSuppressed(DetailSurfaceKind::Wall, uint32_t(wallIndex))
            ? detail.replacementsFor(
                  DetailSurfaceKind::Wall, uint32_t(wallIndex))
            : std::span<bw::core::arr::DetailTriangle const>{};

    auto drawsNormalSide = facesPlayer(orientation);
    auto liquidSurfaceHeight =
        liquidSurfaceHeightFor(orientation, drawsNormalSide);

    if (drawsNormalSide) {
      auto const& properties = worldData.palette[wall.paletteIndex];
      auto resolved = mBakedSurfaceMaterialResolver.resolve(
          properties.wallMaterial, properties.wallEmbossPresetId);
      auto hash = resolved.hash();
      auto mesh = wallRenderer.renderer->getMeshIndexForMaterialHash(
          hash, false, variantFor(wall));
      auto unmappedMesh = wallRenderer.renderer->getMeshIndexForMaterialHash(
          hash, false);
      auto colour = int32_t(wallIndex) == highlightedWall
                        ? lookedAtVertexColour
                        : untintedVertexColour;
      if (!replacements.empty()) {
        for (auto const& replacement : replacements) {
          auto rendered = replacement;
          ApplyWallPhysicalUvToRemainder(orientation, wall, rendered);
          addDetailTriangleToDataProvider(
              wallRenderer.dataProvider,
              replacement.kind ==
                      bw::core::arr::DetailTriangleKind::SurfaceRemainder
                  ? mesh
                  : unmappedMesh,
              rendered, false, colour, liquidSurfaceHeight, orientation.normal.x,
              0.0f, -orientation.normal.y);
        }
        continue;
      }
      auto const& normal = orientation.normal;
      if (projectionData[wallIndex].usesTriplanar) {
        auto triangles = BuildTriplanarWallRenderTriangles(
            worldData, wall, projectionData[wallIndex]);
        for (auto const& triangle : triangles) {
          uint32_t indices[3];
          for (size_t corner = 0; corner < 3; ++corner) {
            auto const& vertex = triangle.vertices[corner];
            indices[corner] = addVertexToDataProvider(
                wallRenderer.dataProvider, mesh, vertex.position.x,
                vertex.elevation, -vertex.position.y, normal.x, 0, -normal.y,
                vertex.u, vertex.v, colour, liquidSurfaceHeight, normal.x,
                0.0f, -normal.y,
                array<float, 3>{vertex.projectionNormal.x, 0.0f,
                                -vertex.projectionNormal.y});
          }
          wallRenderer.dataProvider->addTriangle(
              mesh, indices[0], indices[1], indices[2]);
        }
        continue;
      }
      auto uv = CalculateWallPhysicalUv(orientation, wall);
      auto surface =
          bw::core::arr::BuildArrangementWallSurface(worldData, wall);
      auto addSurfaceVertex = [&](size_t index) {
        auto const& vertex = surface.vertices[index];
        auto u = vertex.endpoint == 0 ? uv.u0 : uv.u1;
        auto v = vertex.topBoundary ? uv.topV[vertex.endpoint]
                                    : uv.bottomV[vertex.endpoint];
        return addVertexToDataProvider(
            wallRenderer.dataProvider, mesh, vertex.position.x,
            vertex.elevation, -vertex.position.y, normal.x, 0, -normal.y, u,
            v, colour, liquidSurfaceHeight, normal.x, 0.0f, -normal.y);
      };
      for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
        auto first = addSurfaceVertex(0);
        auto second = addSurfaceVertex(corner);
        auto third = addSurfaceVertex(corner + 1);
        wallRenderer.dataProvider->addTriangle(mesh, first, second, third);
      }
    } else {
      // The same perimeter and fan as the facesPlayer branch above, with each
      // triangle's vertex order reversed, so the winding and therefore which
      // side is visible is a true mirror image.
      auto mesh = wallRenderer.renderer->getMeshIndexForMaterialHash(backHash, false);
      auto backNormal = -orientation.normal;
      auto colour = int32_t(wallIndex) == highlightedWall
                        ? lookedAtVertexColour
                        : untintedVertexColour;
      if (!replacements.empty()) {
        auto const& properties = worldData.palette[wall.paletteIndex];
        auto resolved = mBakedSurfaceMaterialResolver.resolve(
            properties.wallMaterial, properties.wallEmbossPresetId);
        auto hash = resolved.hash();
        // Chip facets expose a new surface and do not inherit the authored
        // edge's Image. The coplanar remainder is back-facing in this branch.
        auto authoredMesh = wallRenderer.renderer->getMeshIndexForMaterialHash(
            hash, false);
        for (auto const& replacement : replacements) {
          auto followsWall = replacement.followsWallFacing;
          addDetailTriangleToDataProvider(
              wallRenderer.dataProvider,
              followsWall ? mesh : authoredMesh,
              replacement,
              followsWall,
              colour,
              liquidSurfaceHeight);
        }
        continue;
      }
      auto surface =
          bw::core::arr::BuildArrangementWallSurface(worldData, wall);
      auto addSurfaceVertex = [&](size_t index) {
        auto const& vertex = surface.vertices[index];
        return addVertexToDataProvider(
            wallRenderer.dataProvider, mesh, vertex.position.x,
            vertex.elevation, -vertex.position.y, backNormal.x, 0,
            -backNormal.y, float(vertex.endpoint),
            vertex.topBoundary ? 1.0f : 0.0f, colour, liquidSurfaceHeight);
      };
      for (uint8_t corner = 1; corner + 1 < surface.vertexCount; ++corner) {
        auto first = addSurfaceVertex(0);
        auto second = addSurfaceVertex(corner);
        auto third = addSurfaceVertex(corner + 1);
        wallRenderer.dataProvider->addTriangle(mesh, third, second, first);
      }
    }
  }
  wallRenderer.dataProvider->finalizeInternals();
  wallRenderer.dataProvider->setNumPrimitives(wallRenderer.dataProvider->getNumTriangles());
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

  if (highlightedTriangle != mHighlightedTriangle ||
      highlightedCeiling != mHighlightedCeiling) {
    mWorldHasChanged = true;
    mHighlightedTriangle = highlightedTriangle;
    mHighlightedCeiling = highlightedCeiling;
  }
  if (mWorldHasChanged) {
    updateHorizontalDataProvider(
        worldData, highlightedTriangle, highlightedCeiling);
    updateLiquidDataProvider(worldData);
    mWorldHasChanged = false;
  }
  // Unlike the horizontal provider, walls depend on playerPosition, so they
  // need rebuilding on every call - the player moving is reason enough for
  // a wall to flip which single surface it shows, even when nothing about the
  // world itself changed.
  updateWallDataProvider(worldData, playerPosition, highlightedWall);

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
  if (!std::isfinite(liquidEyeSurfaceHeight)) {
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