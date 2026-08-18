#include <common/GameDefines.h>

#include "WorldRenderer.h"
#include "WorldWallOrientation.h"

using namespace std;

WorldRenderer::WorldRenderer(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::Logger* logger)
    : mWorldHasChanged(true), mwLogger(logger) {
  set<string> materialsFound;

  auto defaultMaterial = resourceMgr->getResource("Material.Default", "World");
  auto dataProvider = make_shared<WorldTriangle3dDataProvider>();
  auto renderer = make_shared<WorldRenderer3d>(defaultMaterial, mwLogger);

  mMaterialRenderers.push_back({renderer, dataProvider});
}

WorldRenderer::~WorldRenderer() {
}

void WorldRenderer::createRenderTargets(mpp::RenderSystem* renderSystem) {
  mpp::RenderTextureOptions options;

  // The composite stretches each target over the whole screen, so it samples
  // linearly and clamps at the edge rather than wrapping. MPP takes the OpenGL
  // wire values here: 0x2601 is GL_LINEAR and 0x812F is GL_CLAMP_TO_EDGE.
  options.params.minFilter = 0x2601;
  options.params.magFilter = 0x2601;
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
    auto& [renderer, dataProvider] = item;

    renderer->create(dataProvider, world, renderSystem, resourceMgr);
    renderer->addToScene(scene, world);
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

  auto& matRenderer = mMaterialRenderers[0];
  auto& dataProvider = matRenderer.second;
  std::vector<uint32_t> numTrianglesPerMesh(dataProvider->getNumMeshes());

  for (auto const& triangle : triangles) {
    auto const& face = worldData.faces[triangle.face];
    auto const& properties = worldData.palette[face.paletteIndex];
    auto floorHash = properties.floorMaterialDef.data.hash(
        properties.floorMaterialIndex);
    auto ceilingHash = properties.ceilingMaterialDef.data.hash(
        properties.ceilingMaterialIndex);

    ++numTrianglesPerMesh[matRenderer.first->getMeshIndexForMaterialHash(floorHash)];
    ++numTrianglesPerMesh[matRenderer.first->getMeshIndexForMaterialHash(ceilingHash)];
  }

  for (auto const& wall : walls) {
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto wallHash = properties.wallMaterialDef.data.hash(
        properties.wallMaterialIndex);

    numTrianglesPerMesh[matRenderer.first->getMeshIndexForMaterialHash(wallHash)] += 2;
  }

  dataProvider->updateInternals(numTrianglesPerMesh);

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

    auto floorColour = properties.floorMaterialDef.data.packedColour();
    auto floorHash = properties.floorMaterialDef.data.hash(
        properties.floorMaterialIndex);
    auto floorMeshIndex =
        matRenderer.first->getMeshIndexForMaterialHash(floorHash);
    uint32_t floorIndices[3];
    for (int i = 0; i < 3; ++i) {
      auto uv = positions[i] / 64.0f;
      floorIndices[i] = addVertexToDataProvider(
          dataProvider, floorMeshIndex,
          positions[i].x, properties.floorZ, positions[i].y,
          0, 1, 0, uv.x, uv.y, floorColour);
    }
    dataProvider->addTriangle(
        floorMeshIndex, floorIndices[0], floorIndices[1], floorIndices[2]);

    auto ceilingColour = properties.ceilingMaterialDef.data.packedColour();
    auto ceilingHash = properties.ceilingMaterialDef.data.hash(
        properties.ceilingMaterialIndex);
    auto ceilingMeshIndex =
        matRenderer.first->getMeshIndexForMaterialHash(ceilingHash);
    uint32_t ceilingIndices[3];
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      ceilingIndices[2 - i] = addVertexToDataProvider(
          dataProvider, ceilingMeshIndex,
          positions[i].x, properties.ceilingZ, positions[i].y,
          0, -1, 0, uv.x, uv.y, ceilingColour);
    }
    dataProvider->addTriangle(
        ceilingMeshIndex, ceilingIndices[0], ceilingIndices[1], ceilingIndices[2]);
  }

  for (auto const& wall : walls) {
    auto const& orientation = bw::app::orientArrangementWall(worldData, wall);
    auto const& v0 = orientation.v0;
    auto const& v1 = orientation.v1;
    auto const& normal = orientation.normal;
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto wallColour = properties.wallMaterialDef.data.packedColour();
    auto wallHash = properties.wallMaterialDef.data.hash(
        properties.wallMaterialIndex);
    auto wallMeshIndex =
        matRenderer.first->getMeshIndexForMaterialHash(wallHash);
    auto bottom0 = addVertexToDataProvider(dataProvider, wallMeshIndex, v0.x, wall.minZ, v0.y, normal.x, 0, normal.y, 0, 0, wallColour);
    auto bottom1 = addVertexToDataProvider(dataProvider, wallMeshIndex, v1.x, wall.minZ, v1.y, normal.x, 0, normal.y, 1, 0, wallColour);
    auto top1 = addVertexToDataProvider(dataProvider, wallMeshIndex, v1.x, wall.maxZ, v1.y, normal.x, 0, normal.y, 1, 1, wallColour);
    auto top0 = addVertexToDataProvider(dataProvider, wallMeshIndex, v0.x, wall.maxZ, v0.y, normal.x, 0, normal.y, 0, 1, wallColour);
    dataProvider->addTriangle(wallMeshIndex, bottom0, bottom1, top1);
    dataProvider->addTriangle(wallMeshIndex, top1, top0, bottom0);
  }

  dataProvider->finalizeInternals();

  for (auto& item : mMaterialRenderers) {
    auto& [renderer, provider] = item;
    provider->setNumPrimitives(provider->getNumTriangles());
  }
}

void WorldRenderer::update(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime) {
  BW_UNUSED(world);

  if (mWorldHasChanged) {
    updateDataProviders(worldData);
    mWorldHasChanged = false;
  }

  for (auto& item : mMaterialRenderers) {
    auto& [renderer, dataProvider] = item;

    // Update renderer
    renderer->update(frameTime);
  }
}