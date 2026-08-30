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

}  // namespace

WorldRenderer::WorldRenderer(
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::Logger* logger,
    bw::app::RenderTextureFilter renderTextureFilter,
    bw::app::HorizontalMaterials horizontalMaterials,
    vector<WallRenderSurface> wallRenderSurfaces,
    WallRenderVariantResolver wallRenderVariantResolver,
    string worldResourceNamespace,
    bool deferLiquidToWaterPass)
    : mSubMaterialResolver(resourceMgr),
      mBakedSubMaterialResolver(resourceMgr),
      mResourceMgr(resourceMgr),
      mWorldResourceNamespace(move(worldResourceNamespace)),
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
           &mSubMaterialResolver),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Horizontal});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, fragmentOverdrawMaterial, mwLogger,
           WorldSurfaceSet::Liquid,
           &mSubMaterialResolver, vector<WallRenderSurface>{},
           deferLiquidToWaterPass),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Liquid});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, fragmentOverdrawMaterial, mwLogger,
           WorldSurfaceSet::Walls,
           &mSubMaterialResolver,
           mWallRenderSurfaces),
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
  set<pair<string, string>> wallSurfaceMaterials;
  for (uint32_t primitiveIndex = 0;
       primitiveIndex < world->getNumPrimitives(); ++primitiveIndex) {
    auto* primitive = world->getPrimitive(primitiveIndex);
    auto const& properties = primitive->getProperties();
    wallSurfaceMaterials.emplace(
        properties.wallMaterialId, properties.wallEmbossPresetId);
    for (auto const& polygon : primitive->getVertices()) {
      for (auto const& ring : polygon) {
        for (auto const& vertex : ring) {
          auto image = vertex.edgeNormalMap.imageData();
          if (!image) continue;
          auto identity = normalMapIdentity(*image);
          if (!mNormalMapVariants.contains(identity)) {
            string namesp;
            string name;
            wp::application::resourcesystem::Resource::splitName(
                image->resourceName, mWorldResourceNamespace, &namesp, &name);
            auto resource = mResourceMgr->getResource(name, namesp);
            auto imageResource = dynamic_pointer_cast<
                wp::application::resourcesystem::ImageResource>(resource);
            if (!imageResource || !mResourceMgr->isResourceLoaded(resource)) {
              throw runtime_error("Wall normal-map resource '" +
                                  image->resourceName +
                                  "' is not a loaded ImageResource.");
            }
            if (imageResource->getWidth() <= 0 ||
                imageResource->getHeight() <= 0 ||
                imageResource->getWidth() > 8192 ||
                imageResource->getHeight() > 8192 ||
                (imageResource->getNumChannels() != 3 &&
                 imageResource->getNumChannels() != 4)) {
              throw runtime_error("Wall normal-map ImageResource '" +
                                  image->resourceName +
                                  "' has unsupported dimensions or channels.");
            }
            WallRenderVariant variant;
            variant.identity = identity;
            variant.textureSampler = "TEX1";
            variant.texture = imageResource->getMppResource();
            auto strength = image->strength;
            auto aspectRatio = static_cast<float>(imageResource->getWidth()) /
                               imageResource->getHeight();
            variant.setUniforms =
                [strength, aspectRatio](mpp::UniformCollection& uniforms) {
                  uniforms.updateUniform("WALL_NORMAL_MAP_ENABLED", int32_t{1});
                  uniforms.updateUniform("WALL_NORMAL_MAP_STRENGTH", strength);
                  uniforms.updateUniform(
                      "WALL_NORMAL_MAP_ASPECT_RATIO", aspectRatio);
                };
            mNormalMapVariants.emplace(identity, move(variant));
          }
        }
      }
    }
  }
  // The winning override and the wall's Sub-material can come from different
  // Primitives in the fold.  Predeclare their complete cross-product so a
  // resolved Image always has a bucket; Unset and Disabled intentionally use
  // the single existing unmapped bucket.
  set<tuple<string, string, string>> seeded;
  for (auto const& [subMaterialId, embossPresetId] : wallSurfaceMaterials) {
    for (auto const& [identity, variant] : mNormalMapVariants) {
      if (seeded.emplace(subMaterialId, embossPresetId, identity).second) {
        mWallRenderSurfaces.push_back(
            {subMaterialId, variant, embossPresetId});
      }
    }
  }
  auto fallbackResolver = move(mWallRenderVariantResolver);
  mWallRenderVariantResolver =
      [this, fallbackResolver = move(fallbackResolver)](
          bw::core::arr::ArrangementWall const& wall)
      -> optional<WallRenderVariant> {
    if (auto image = wall.normalMapOverride.imageData()) {
      auto found = mNormalMapVariants.find(normalMapIdentity(*image));
      if (found != mNormalMapVariants.end()) return found->second;
    }
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
    if (properties.floorMaterialId == subMaterialId)
      surfaces.emplace(properties.floorEmbossPresetId, true);
    if (properties.ceilingMaterialId == subMaterialId)
      surfaces.emplace(properties.ceilingEmbossPresetId, false);
    if (properties.wallMaterialId == subMaterialId)
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

  set<tuple<string, bool>> surfaces;
  for (uint32_t i = 0; i < mwWorld->getNumPrimitives(); ++i) {
    auto const& properties = mwWorld->getPrimitive(i)->getProperties();
    if (properties.floorEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.floorMaterialId, true);
    if (properties.ceilingEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.ceilingMaterialId, false);
    if (properties.wallEmbossPresetId == embossPresetId)
      surfaces.emplace(properties.wallMaterialId, false);
  }

  for (auto const& [subMaterialId, floor] : surfaces) {
    auto baked = mBakedSubMaterialResolver.resolve(
        subMaterialId, embossPresetId);
    auto composed = mSubMaterialResolver.resolve(
        subMaterialId, embossPresetId);
    composed.def.emboss = emboss;
    auto bakedHash = baked.def.hash(baked.materialIndex);
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
    float liquidSurfaceHeight) {
  WorldTriangle3dDataProvider::DrawVert vertex{
      {px, py, pz}, {nx, ny, nz}, {u, v}, c, liquidSurfaceHeight};
  return dataProvider->addVertex(meshIndex, vertex);
}

void WorldRenderer::addDetailTriangleToDataProvider(
    DataProvider dataProvider,
    uint32_t meshIndex,
    bw::core::arr::DetailTriangle const& triangle,
    bool mirrored,
    uint32_t colour,
    float liquidSurfaceHeight) {
  // Arrangement space keeps height in z and renderer space keeps it in y,
  // mapping (x, y, z) to (x, z, -y) - a rotation, not a reflection, so a
  // triangle wound counter-clockwise about its normal there stays wound
  // counter-clockwise about the mapped normal here and the indices pass
  // through in order. The world draws unculled either way (which is why each
  // wall picks one quad per frame rather than emitting both sides); what
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
        liquidSurfaceHeight);
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
  auto const& liquidDepths = snapshot.getLiquidDepths();
  auto liquidSurfaceHeightFor =
      [&](bw::core::arr::DetailSurfaceKey const& source) {
        if (source.kind == DetailSurfaceKind::Wall ||
            liquidDepths[source.index] <= 0.0f) {
          return WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;
        }
        auto const& sourceProperties =
            worldData.palette[worldData.faces[source.index].paletteIndex];
        return sourceProperties.floorZ + liquidDepths[source.index];
      };

  // A rebuilt face's replacements resolve exactly the Sub-material the face
  // itself would have, so they land in mesh buckets that already exist.
  auto horizontalMeshFor = [&](bw::core::arr::DetailSurfaceKey const& key) {
    auto const& properties =
        worldData.palette[worldData.faces[key.index].paletteIndex];
    auto isFloor = key.kind == DetailSurfaceKind::FloorOfFace;
    auto resolved = mBakedSubMaterialResolver.resolve(
        isFloor ? properties.floorMaterialId : properties.ceilingMaterialId,
        isFloor ? properties.floorEmbossPresetId
                : properties.ceilingEmbossPresetId);
    return horizontal.renderer->getMeshIndexForMaterialHash(
        resolved.def.hash(resolved.materialIndex), isFloor);
  };

  std::vector<uint32_t> horizontalCounts(
      horizontal.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    auto const& properties = worldData.palette[worldData.faces[triangle.face].paletteIndex];
    auto floorResolved = mBakedSubMaterialResolver.resolve(
        properties.floorMaterialId, properties.floorEmbossPresetId);
    auto floorHash = floorResolved.def.hash(floorResolved.materialIndex);
    auto ceilingResolved = mBakedSubMaterialResolver.resolve(
        properties.ceilingMaterialId, properties.ceilingEmbossPresetId);
    auto ceilingHash = ceilingResolved.def.hash(ceilingResolved.materialIndex);
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

    auto liquidSurfaceHeight =
        liquidDepths[triangle.face] > 0.0f
            ? properties.floorZ + liquidDepths[triangle.face]
            : WorldTriangle3dDataProvider::dryLiquidSurfaceHeight;

    if (!detail.isSuppressed(DetailSurfaceKind::FloorOfFace, triangle.face)) {
      auto floorResolved = mBakedSubMaterialResolver.resolve(
          properties.floorMaterialId, properties.floorEmbossPresetId);
      auto floorHash = floorResolved.def.hash(floorResolved.materialIndex);
      auto floorMesh = horizontal.renderer->getMeshIndexForMaterialHash(
          floorHash, true);
      uint32_t floorIndices[3];
      for (int i = 0; i < 3; ++i) {
        auto uv = positions[i] / 64.0f;
        // Reflecting authored Y into renderer -Z reverses winding, so reverse
        // the indices as well to preserve the floor's front face.
        floorIndices[2 - i] = addVertexToDataProvider(
            horizontal.dataProvider, floorMesh, positions[i].x,
            properties.floorZ, -positions[i].y, 0, 1, 0, uv.x, uv.y,
            triangle.face == highlightedFace && !highlightedCeiling
                ? lookedAtVertexColour
                : untintedVertexColour,
            liquidSurfaceHeight);
      }
      horizontal.dataProvider->addTriangle(
          floorMesh, floorIndices[0], floorIndices[1], floorIndices[2]);
    }

    if (detail.isSuppressed(DetailSurfaceKind::CeilingOfFace, triangle.face)) {
      continue;
    }
    auto ceilingResolved = mBakedSubMaterialResolver.resolve(
        properties.ceilingMaterialId, properties.ceilingEmbossPresetId);
    auto ceilingHash = ceilingResolved.def.hash(ceilingResolved.materialIndex);
    auto ceilingMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false);
    uint32_t ceilingIndices[3];
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      ceilingIndices[i] = addVertexToDataProvider(
          horizontal.dataProvider, ceilingMesh, positions[i].x,
          properties.ceilingZ, -positions[i].y, 0, -1, 0, uv.x, uv.y,
          triangle.face == highlightedFace && highlightedCeiling
              ? lookedAtVertexColour
              : untintedVertexColour,
          liquidSurfaceHeight);
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
    addDetailTriangleToDataProvider(
        horizontal.dataProvider, horizontalMeshFor(replacement.source),
        replacement, false,
        highlighted ? lookedAtVertexColour : untintedVertexColour,
        liquidSurfaceHeightFor(replacement.source));
  }
  horizontal.dataProvider->finalizeInternals();
  horizontal.dataProvider->setNumPrimitives(horizontal.dataProvider->getNumTriangles());
}

void WorldRenderer::updateLiquidDataProvider(
    bw::core::WorldData const& snapshot) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getTriangles();
  auto const& liquidDepths = snapshot.getLiquidDepths();
  auto& liquid = mMaterialRenderers[1];
  auto liquidHashFor = [](bw::core::LiquidType liquidType) {
    return bw::core::MaterialDefinition{}.data.hash(
        bw::core::LiquidMaterialIndex(liquidType));
  };

  std::vector<uint32_t> counts(liquid.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    if (liquidDepths[triangle.face] <= 0.0f) continue;
    auto const& properties =
        worldData.palette[worldData.faces[triangle.face].paletteIndex];
    ++counts[liquid.renderer->getMeshIndexForMaterialHash(
        liquidHashFor(properties.liquidType), true)];
  }
  liquid.dataProvider->updateInternals(counts);

  for (auto const& triangle : triangles) {
    auto liquidDepth = liquidDepths[triangle.face];
    if (liquidDepth <= 0.0f) continue;
    auto const& properties =
        worldData.palette[worldData.faces[triangle.face].paletteIndex];
    auto mesh = liquid.renderer->getMeshIndexForMaterialHash(
        liquidHashFor(properties.liquidType), true);
    auto liquidZ = properties.floorZ + liquidDepth;
    uint32_t indices[3];
    for (int i = 0; i < 3; ++i) {
      auto const& vertex = worldData.vertices[triangle.v[i]];
      wp::Vector2 position{
          bw::core::arr::ToWorldCoordinate(vertex.x),
          bw::core::arr::ToWorldCoordinate(vertex.y)};
      auto uv = position / 64.0f;
      // Reflecting authored Y into renderer -Z reverses winding.
      indices[2 - i] = addVertexToDataProvider(
          liquid.dataProvider, mesh, position.x, liquidZ, -position.y,
          0, 1, 0, uv.x, uv.y, transparentVertexColour, liquidZ);
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
  auto& wallRenderer = mMaterialRenderers[2];

  // Walls render two-sided, but only ever as a single quad: whichever side
  // currently faces the player keeps the wall's authored material: this
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
  // plain quad. Only the coplanar wall remainder follows the player-facing
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
    auto resolved = mBakedSubMaterialResolver.resolve(
        properties.wallMaterialId, properties.wallEmbossPresetId);
    auto hash = resolved.def.hash(resolved.materialIndex);
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
      } else {
        wallCounts[authoredMesh] += 2u;
      }
    } else if (suppressed) {
      for (auto const& replacement : replacements) {
        ++wallCounts[replacement.followsWallFacing ? backMesh
                                                   : unmappedAuthoredMesh];
      }
    } else {
      wallCounts[backMesh] += 2u;
    }
  }
  wallRenderer.dataProvider->updateInternals(wallCounts);

  for (size_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto orientation = bw::core::arr::OrientArrangementWall(worldData, wall);
    auto const& v0 = orientation.v0;
    auto const& v1 = orientation.v1;
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
      auto resolved = mBakedSubMaterialResolver.resolve(
          properties.wallMaterialId, properties.wallEmbossPresetId);
      auto hash = resolved.def.hash(resolved.materialIndex);
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
              rendered, false, colour, liquidSurfaceHeight);
        }
        continue;
      }
      auto const& normal = orientation.normal;
      auto uv = CalculateWallPhysicalUv(orientation, wall);
      auto bottom0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.minZ, -v0.y,
          normal.x, 0, -normal.y, uv.u0, uv.minV, colour,
          liquidSurfaceHeight);
      auto bottom1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.minZ, -v1.y,
          normal.x, 0, -normal.y, uv.u1, uv.minV, colour,
          liquidSurfaceHeight);
      auto top1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.maxZ, -v1.y,
          normal.x, 0, -normal.y, uv.u1, uv.maxV, colour,
          liquidSurfaceHeight);
      auto top0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.maxZ, -v0.y,
          normal.x, 0, -normal.y, uv.u0, uv.maxV, colour,
          liquidSurfaceHeight);
      wallRenderer.dataProvider->addTriangle(mesh, top1, bottom1, bottom0);
      wallRenderer.dataProvider->addTriangle(mesh, bottom0, top0, top1);
    } else {
      // The same 4 corners and diagonal as the facesPlayer branch above,
      // with each triangle's vertex order reversed - not a different
      // diagonal - so the winding, and therefore which side it's visible
      // from, is a true mirror image rather than an inconsistent one.
      auto mesh = wallRenderer.renderer->getMeshIndexForMaterialHash(backHash, false);
      auto backNormal = -orientation.normal;
      auto colour = int32_t(wallIndex) == highlightedWall
                        ? lookedAtVertexColour
                        : untintedVertexColour;
      if (!replacements.empty()) {
        auto const& properties = worldData.palette[wall.paletteIndex];
        auto resolved = mBakedSubMaterialResolver.resolve(
            properties.wallMaterialId, properties.wallEmbossPresetId);
        auto hash = resolved.def.hash(resolved.materialIndex);
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
      auto bottom0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.minZ, -v0.y,
          backNormal.x, 0, -backNormal.y, 0, 0, colour,
          liquidSurfaceHeight);
      auto bottom1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.minZ, -v1.y,
          backNormal.x, 0, -backNormal.y, 1, 0, colour,
          liquidSurfaceHeight);
      auto top1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.maxZ, -v1.y,
          backNormal.x, 0, -backNormal.y, 1, 1, colour,
          liquidSurfaceHeight);
      auto top0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.maxZ, -v0.y,
          backNormal.x, 0, -backNormal.y, 0, 1, colour,
          liquidSurfaceHeight);
      wallRenderer.dataProvider->addTriangle(mesh, bottom0, bottom1, top1);
      wallRenderer.dataProvider->addTriangle(mesh, top1, top0, bottom0);
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
    float farGridSize,
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
  // a wall to flip which single quad it shows, even when nothing about the
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
        farGridSize, secondaryMaterial, frameTime);
  }
}