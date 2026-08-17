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

void WorldRenderer::addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c) {
  auto vertexPtr = dataProvider->nextVertexPtr(meshIndex);

  vertexPtr->pos[0] = px;
  vertexPtr->pos[1] = py;
  vertexPtr->pos[2] = pz;
  vertexPtr->nor[0] = nx;
  vertexPtr->nor[1] = ny;
  vertexPtr->nor[2] = nz;
  vertexPtr->tex[0] = u;
  vertexPtr->tex[1] = v;
  vertexPtr->col = c;
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
    for (int i = 0; i < 3; ++i) {
      auto uv = positions[i] / 64.0f;
      addVertexToDataProvider(
          dataProvider, floorMeshIndex,
          positions[i].x, properties.floorZ, positions[i].y,
          0, 1, 0, uv.x, uv.y, floorColour);
    }
    auto numIndices = dataProvider->getNumIndices(floorMeshIndex);
    dataProvider->addTriangle(
        floorMeshIndex, numIndices, numIndices + 1, numIndices + 2);

    auto ceilingColour = properties.ceilingMaterialDef.data.packedColour();
    auto ceilingHash = properties.ceilingMaterialDef.data.hash(
        properties.ceilingMaterialIndex);
    auto ceilingMeshIndex =
        matRenderer.first->getMeshIndexForMaterialHash(ceilingHash);
    for (int i = 2; i >= 0; --i) {
      auto uv = positions[i] / 64.0f;
      addVertexToDataProvider(
          dataProvider, ceilingMeshIndex,
          positions[i].x, properties.ceilingZ, positions[i].y,
          0, -1, 0, uv.x, uv.y, ceilingColour);
    }
    numIndices = dataProvider->getNumIndices(ceilingMeshIndex);
    dataProvider->addTriangle(
        ceilingMeshIndex, numIndices, numIndices + 1, numIndices + 2);
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
    auto numIndices = dataProvider->getNumIndices(wallMeshIndex);

    addVertexToDataProvider(dataProvider, wallMeshIndex, v0.x, wall.minZ, v0.y, normal.x, 0, normal.y, 0, 0, wallColour);
    addVertexToDataProvider(dataProvider, wallMeshIndex, v1.x, wall.minZ, v1.y, normal.x, 0, normal.y, 1, 0, wallColour);
    addVertexToDataProvider(dataProvider, wallMeshIndex, v1.x, wall.maxZ, v1.y, normal.x, 0, normal.y, 1, 1, wallColour);
    addVertexToDataProvider(dataProvider, wallMeshIndex, v1.x, wall.maxZ, v1.y, normal.x, 0, normal.y, 1, 1, wallColour);
    addVertexToDataProvider(dataProvider, wallMeshIndex, v0.x, wall.maxZ, v0.y, normal.x, 0, normal.y, 0, 1, wallColour);
    addVertexToDataProvider(dataProvider, wallMeshIndex, v0.x, wall.minZ, v0.y, normal.x, 0, normal.y, 0, 0, wallColour);
    dataProvider->addTriangle(
        wallMeshIndex, numIndices, numIndices + 1, numIndices + 2);
    dataProvider->addTriangle(
        wallMeshIndex, numIndices + 3, numIndices + 4, numIndices + 5);
  }

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