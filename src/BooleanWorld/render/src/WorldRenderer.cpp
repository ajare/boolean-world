#include <common/GameDefines.h>

#include <core/Defines.h>
#include <core/MaterialDefinition.h>

#include "WorldRenderer.h"
#include "WorldWallOrientation.h"

using namespace std;

namespace {

// world_pbr.frag multiplies a surface's own colour by its vertex colour
// before lighting, so anything wanting the material rendered as authored
// passes white. The editor's 3D preview is the one caller that does not,
// tinting the surface the viewer is looking at.
constexpr uint32_t untintedVertexColour = 0xffffffffu;
// Full red/green, 35% blue: the preview's established looked-at tint.
constexpr uint32_t lookedAtVertexColour = 0xff59ffffu;

}  // namespace

WorldRenderer::WorldRenderer(
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::Logger* logger,
    bw::app::RenderTextureFilter renderTextureFilter,
    bw::app::HorizontalMaterials horizontalMaterials)
    : mSubMaterialResolver(resourceMgr),
      mBakedSubMaterialResolver(resourceMgr),
      mWorldHasChanged(true),
      mwLogger(logger),
      mRenderTextureFilter(renderTextureFilter) {
  auto material3d = resourceMgr->getResource("Material.Default", "World");
  auto horizontalMaterial = horizontalMaterials ==
                                    bw::app::HorizontalMaterials::TwoDimensional
                                ? resourceMgr->getResource(
                                      "Material.Horizontal2d", "World")
                                : material3d;

  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           horizontalMaterial, mwLogger, WorldSurfaceSet::Horizontal,
           &mSubMaterialResolver),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Horizontal});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, mwLogger, WorldSurfaceSet::Walls,
           &mSubMaterialResolver),
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

void WorldRenderer::create(mpp::ScenePtr scene, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  for (auto& item : mMaterialRenderers) {
    item.renderer->create(
        item.dataProvider, world, renderSystem, resourceMgr);
    item.renderer->addToScene(scene, world);
  }
}

void WorldRenderer::setWorldChanged() {
  mWorldHasChanged = true;
}

void WorldRenderer::updateSubMaterialDraft(
    string const& subMaterialId, int32_t materialIndex,
    bw::core::MaterialDefinitionData const& definition) {
  // The batch key is the definition resolved when this preview opened, not
  // the draft's changing hash. Locate that baked key first, then replace only
  // its shader uniforms. Each surface set owns independent buckets.
  auto baked = mBakedSubMaterialResolver.resolve(subMaterialId);
  auto bakedHash = baked.def.hash(baked.materialIndex);
  for (auto const& item : mMaterialRenderers) {
    if (item.surfaceSet == WorldSurfaceSet::Horizontal) {
      item.renderer->updateMaterialUniforms(
          bakedHash, true, materialIndex, definition);
      item.renderer->updateMaterialUniforms(
          bakedHash, false, materialIndex, definition);
    } else {
      item.renderer->updateMaterialUniforms(
          bakedHash, false, materialIndex, definition);
    }
  }
}

void WorldRenderer::reloadSubMaterialResolver(
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  mSubMaterialResolver = SubMaterialResolver(resourceMgr);
}

uint32_t WorldRenderer::addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c) {
  WorldTriangle3dDataProvider::DrawVert vertex{
      {px, py, pz}, {nx, ny, nz}, {u, v}, c};
  return dataProvider->addVertex(meshIndex, vertex);
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

  std::vector<uint32_t> horizontalCounts(
      horizontal.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    auto const& properties = worldData.palette[worldData.faces[triangle.face].paletteIndex];
    auto floorResolved = mBakedSubMaterialResolver.resolve(properties.floorMaterialId);
    auto floorHash = floorResolved.def.hash(floorResolved.materialIndex);
    auto ceilingResolved = mBakedSubMaterialResolver.resolve(properties.ceilingMaterialId);
    auto ceilingHash = ceilingResolved.def.hash(ceilingResolved.materialIndex);
    ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
        floorHash, true)];
    ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false)];
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

    auto floorResolved = mBakedSubMaterialResolver.resolve(properties.floorMaterialId);
    auto floorHash = floorResolved.def.hash(floorResolved.materialIndex);
    auto floorMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        floorHash, true);
    uint32_t floorIndices[3];
    for (int i = 0; i < 3; ++i) {
      auto uv = positions[i] / 64.0f;
      floorIndices[i] = addVertexToDataProvider(
          horizontal.dataProvider, floorMesh, positions[i].x,
          properties.floorZ, positions[i].y, 0, 1, 0, uv.x, uv.y,
          triangle.face == highlightedFace && !highlightedCeiling
              ? lookedAtVertexColour
              : untintedVertexColour);
    }
    horizontal.dataProvider->addTriangle(
        floorMesh, floorIndices[0], floorIndices[1], floorIndices[2]);

    auto ceilingResolved = mBakedSubMaterialResolver.resolve(properties.ceilingMaterialId);
    auto ceilingHash = ceilingResolved.def.hash(ceilingResolved.materialIndex);
    auto ceilingMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false);
    uint32_t ceilingIndices[3];
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      ceilingIndices[2 - i] = addVertexToDataProvider(
          horizontal.dataProvider, ceilingMesh, positions[i].x,
          properties.ceilingZ, positions[i].y, 0, -1, 0, uv.x, uv.y,
          triangle.face == highlightedFace && highlightedCeiling
              ? lookedAtVertexColour
              : untintedVertexColour);
    }
    horizontal.dataProvider->addTriangle(
        ceilingMesh, ceilingIndices[0], ceilingIndices[1], ceilingIndices[2]);
  }
  horizontal.dataProvider->finalizeInternals();
  horizontal.dataProvider->setNumPrimitives(horizontal.dataProvider->getNumTriangles());
}

void WorldRenderer::updateWallDataProvider(
    bw::core::WorldData const& snapshot, glm::vec3 const& playerPosition,
    int32_t highlightedWall) {
  auto const& worldData = snapshot.getArrangement();
  auto const& walls = snapshot.getWalls();
  auto& wallRenderer = mMaterialRenderers[1];

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
  wp::Vector2 playerPositionXZ{playerPosition.x, playerPosition.z};

  auto facesPlayer = [&](bw::app::ArrangementWallOrientation const& orientation) {
    auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
    return orientation.normal.dot(playerPositionXZ - midpoint) > 0.0f;
  };

  std::vector<uint32_t> wallCounts(wallRenderer.dataProvider->getNumMeshes());
  for (auto const& wall : walls) {
    if (!wall.visible) {
      continue;
    }
    auto orientation = bw::app::orientArrangementWall(worldData, wall);
    if (facesPlayer(orientation)) {
      auto const& properties = worldData.palette[wall.paletteIndex];
      auto resolved = mBakedSubMaterialResolver.resolve(properties.wallMaterialId);
      auto hash = resolved.def.hash(resolved.materialIndex);
      wallCounts[wallRenderer.renderer->getMeshIndexForMaterialHash(
          hash, false)] += 2;
    } else {
      wallCounts[wallRenderer.renderer->getMeshIndexForMaterialHash(
          backHash, false)] += 2;
    }
  }
  wallRenderer.dataProvider->updateInternals(wallCounts);

  for (size_t wallIndex = 0; wallIndex < walls.size(); ++wallIndex) {
    auto const& wall = walls[wallIndex];
    if (!wall.visible) {
      continue;
    }
    auto orientation = bw::app::orientArrangementWall(worldData, wall);
    auto const& v0 = orientation.v0;
    auto const& v1 = orientation.v1;

    if (facesPlayer(orientation)) {
      auto const& properties = worldData.palette[wall.paletteIndex];
      auto resolved = mBakedSubMaterialResolver.resolve(properties.wallMaterialId);
      auto hash = resolved.def.hash(resolved.materialIndex);
      auto mesh = wallRenderer.renderer->getMeshIndexForMaterialHash(hash, false);
      auto colour = int32_t(wallIndex) == highlightedWall
                        ? lookedAtVertexColour
                        : untintedVertexColour;
      auto const& normal = orientation.normal;
      auto bottom0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.minZ, v0.y,
          normal.x, 0, normal.y, 0, 0, colour);
      auto bottom1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.minZ, v1.y,
          normal.x, 0, normal.y, 1, 0, colour);
      auto top1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.maxZ, v1.y,
          normal.x, 0, normal.y, 1, 1, colour);
      auto top0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.maxZ, v0.y,
          normal.x, 0, normal.y, 0, 1, colour);
      wallRenderer.dataProvider->addTriangle(mesh, bottom0, bottom1, top1);
      wallRenderer.dataProvider->addTriangle(mesh, top1, top0, bottom0);
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
      auto bottom0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.minZ, v0.y,
          backNormal.x, 0, backNormal.y, 0, 0, colour);
      auto bottom1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.minZ, v1.y,
          backNormal.x, 0, backNormal.y, 1, 0, colour);
      auto top1 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v1.x, wall.maxZ, v1.y,
          backNormal.x, 0, backNormal.y, 1, 1, colour);
      auto top0 = addVertexToDataProvider(
          wallRenderer.dataProvider, mesh, v0.x, wall.maxZ, v0.y,
          backNormal.x, 0, backNormal.y, 0, 1, colour);
      wallRenderer.dataProvider->addTriangle(mesh, top1, bottom1, bottom0);
      wallRenderer.dataProvider->addTriangle(mesh, bottom0, top0, top1);
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
    mWorldHasChanged = false;
  }
  // Unlike the horizontal provider, walls depend on playerPosition, so they
  // need rebuilding on every call - the player moving is reason enough for
  // a wall to flip which single quad it shows, even when nothing about the
  // world itself changed.
  updateWallDataProvider(worldData, playerPosition, highlightedWall);

  for (auto& item : mMaterialRenderers) {
    auto const materialIndexOverride =
        item.surfaceSet == WorldSurfaceSet::Walls
            ? wallMaterialIndexOverride
            : horizontalMaterialIndexOverride;
    item.renderer->update(
        playerPosition, lightPosition, materialIndexOverride, materialScale,
        farGridSize, secondaryMaterial, frameTime);
  }
}