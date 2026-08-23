#include <common/GameDefines.h>

#include "WorldRenderer.h"
#include "WorldWallOrientation.h"

using namespace std;

WorldRenderer::WorldRenderer(
    wp::application::resourcesystem::ResourceManager* resourceMgr,
    wp::Logger* logger,
    bw::app::RenderTextureFilter renderTextureFilter,
    bw::app::HorizontalMaterials horizontalMaterials)
    : mWorldHasChanged(true),
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
           horizontalMaterial, mwLogger, WorldSurfaceSet::Horizontal),
       make_shared<WorldTriangle3dDataProvider>(),
       WorldSurfaceSet::Horizontal});
  mMaterialRenderers.push_back(
      {make_shared<WorldRenderer3d>(
           material3d, mwLogger, WorldSurfaceSet::Walls),
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

uint32_t WorldRenderer::addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c) {
  WorldTriangle3dDataProvider::DrawVert vertex{
      {px, py, pz}, {nx, ny, nz}, {u, v}, c};
  return dataProvider->addVertex(meshIndex, vertex);
}

void WorldRenderer::updateDataProviders(bw::core::WorldData const& snapshot) {
  auto const& worldData = snapshot.getArrangement();
  auto const& triangles = snapshot.getTriangles();
  auto const& walls = snapshot.getWalls();
  auto& horizontal = mMaterialRenderers[0];
  auto& wallRenderer = mMaterialRenderers[1];

  std::vector<uint32_t> horizontalCounts(
      horizontal.dataProvider->getNumMeshes());
  for (auto const& triangle : triangles) {
    auto const& properties = worldData.palette[
        worldData.faces[triangle.face].paletteIndex];
    auto floorHash = properties.floorMaterialDef.data.hash(
        properties.floorMaterialIndex);
    auto ceilingHash = properties.ceilingMaterialDef.data.hash(
        properties.ceilingMaterialIndex);
    ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
        floorHash, true)];
    ++horizontalCounts[horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false)];
  }
  horizontal.dataProvider->updateInternals(horizontalCounts);

  for (auto const& triangle : triangles) {
    auto const& face = worldData.faces[triangle.face];
    auto const& properties = worldData.palette[face.paletteIndex];
    wp::Vector2 positions[3];
    for (int i = 0; i < 3; ++i) {
      auto const& vertex = worldData.vertices[triangle.v[i]];
      positions[i] = {
          bw::core::arr::ToWorldCoordinate(vertex.x),
          bw::core::arr::ToWorldCoordinate(vertex.y)};
    }

    auto floorHash = properties.floorMaterialDef.data.hash(
        properties.floorMaterialIndex);
    auto floorMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        floorHash, true);
    uint32_t floorIndices[3];
    for (int i = 0; i < 3; ++i) {
      auto uv = positions[i] / 64.0f;
      floorIndices[i] = addVertexToDataProvider(
          horizontal.dataProvider, floorMesh, positions[i].x,
          properties.floorZ, positions[i].y, 0, 1, 0, uv.x, uv.y,
          properties.floorMaterialDef.data.packedColour());
    }
    horizontal.dataProvider->addTriangle(
        floorMesh, floorIndices[0], floorIndices[1], floorIndices[2]);

    auto ceilingHash = properties.ceilingMaterialDef.data.hash(
        properties.ceilingMaterialIndex);
    auto ceilingMesh = horizontal.renderer->getMeshIndexForMaterialHash(
        ceilingHash, false);
    uint32_t ceilingIndices[3];
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      ceilingIndices[2 - i] = addVertexToDataProvider(
          horizontal.dataProvider, ceilingMesh, positions[i].x,
          properties.ceilingZ, positions[i].y, 0, -1, 0, uv.x, uv.y,
          properties.ceilingMaterialDef.data.packedColour());
    }
    horizontal.dataProvider->addTriangle(
        ceilingMesh, ceilingIndices[0], ceilingIndices[1], ceilingIndices[2]);
  }
  horizontal.dataProvider->finalizeInternals();

  std::vector<uint32_t> wallCounts(wallRenderer.dataProvider->getNumMeshes());
  for (auto const& wall : walls) {
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto hash = properties.wallMaterialDef.data.hash(
        properties.wallMaterialIndex);
    wallCounts[wallRenderer.renderer->getMeshIndexForMaterialHash(
        hash, false)] += 2;
  }
  wallRenderer.dataProvider->updateInternals(wallCounts);

  for (auto const& wall : walls) {
    auto const orientation = bw::app::orientArrangementWall(worldData, wall);
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto hash = properties.wallMaterialDef.data.hash(
        properties.wallMaterialIndex);
    auto mesh = wallRenderer.renderer->getMeshIndexForMaterialHash(hash, false);
    auto colour = properties.wallMaterialDef.data.packedColour();
    auto const& v0 = orientation.v0;
    auto const& v1 = orientation.v1;
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
  }
  wallRenderer.dataProvider->finalizeInternals();

  for (auto& item : mMaterialRenderers) {
    item.dataProvider->setNumPrimitives(
        item.dataProvider->getNumTriangles());
  }
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
    FloorPatternOptions const& floorPattern,
    float frameTime) {
  BW_UNUSED(world);

  if (mWorldHasChanged) {
    updateDataProviders(worldData);
    mWorldHasChanged = false;
  }

  for (auto& item : mMaterialRenderers) {
    auto const materialIndexOverride =
        item.surfaceSet == WorldSurfaceSet::Walls
            ? wallMaterialIndexOverride
            : horizontalMaterialIndexOverride;
    item.renderer->update(
        playerPosition, lightPosition, materialIndexOverride, materialScale,
        farGridSize, floorPattern, frameTime);
  }
}