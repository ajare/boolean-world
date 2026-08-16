#include <common/GameDefines.h>

#include <core/ClipperDefines.h>
#include <core/DynamicWorldDataGenerator.h>

#include "WorldRenderer.h"

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

void WorldRenderer::updateDataProviders(expr::ArrangementResult const& worldData) {
  auto triangles = expr::BuildArrangementTriangles(worldData);
  auto walls = expr::BuildArrangementWalls(worldData);
  auto maxNumTrianglePrimitives = triangles.size() * 2 + walls.size() * 2;

  for (auto& item : mMaterialRenderers) {
    auto& [renderer, dataProvider] = item;
    dataProvider->clear();
    dataProvider->updateInternals(
        uint32_t(maxNumTrianglePrimitives * 3),
        uint32_t(maxNumTrianglePrimitives));
  }

  auto& matRenderer = mMaterialRenderers[0];
  auto& dataProvider = matRenderer.second;

  for (auto const& triangle : triangles) {
    auto const& face = worldData.faces[triangle.face];
    auto const& properties = worldData.palette[face.paletteIndex];
    wp::Vector2 positions[3];
    for (int i = 0; i < 3; ++i) {
      auto const& vertex = worldData.vertices[triangle.v[i]];
      positions[i] = {
          float(vertex.x / BW_CLIPPER_SCALE),
          float(vertex.y / BW_CLIPPER_SCALE)};
    }

    auto floorColour = properties.floorMaterialDef.data.baseColourUint;
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
    auto numIndices =
        uint16_t(dataProvider->getNumIndices(floorMeshIndex));
    dataProvider->addTriangle(
        floorMeshIndex, numIndices, numIndices + 1, numIndices + 2);

    auto ceilingColour = properties.ceilingMaterialDef.data.baseColourUint;
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
    numIndices = uint16_t(dataProvider->getNumIndices(ceilingMeshIndex));
    dataProvider->addTriangle(
        ceilingMeshIndex, numIndices, numIndices + 1, numIndices + 2);
  }

  for (auto const& wall : walls) {
    auto const& edge = worldData.edges[wall.edge];
    auto const& fixed0 = worldData.vertices[edge.v[0]];
    auto const& fixed1 = worldData.vertices[edge.v[1]];
    wp::Vector2 v0{
        float(fixed0.x / BW_CLIPPER_SCALE),
        float(fixed0.y / BW_CLIPPER_SCALE)};
    wp::Vector2 v1{
        float(fixed1.x / BW_CLIPPER_SCALE),
        float(fixed1.y / BW_CLIPPER_SCALE)};
    auto normal = (v1 - v0).normalisedCopy().perpendicular();
    auto const& properties = worldData.palette[wall.paletteIndex];
    auto wallColour = properties.wallMaterialDef.data.baseColourUint;
    auto wallHash = properties.wallMaterialDef.data.hash(
        properties.wallMaterialIndex);
    auto wallMeshIndex =
        matRenderer.first->getMeshIndexForMaterialHash(wallHash);
    auto numIndices = uint16_t(dataProvider->getNumIndices(wallMeshIndex));

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
  BW_UNUSED(worldData);

  if (mWorldHasChanged) {
    auto dynamicGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
        world->getWorldDataGenerator());
    if (dynamicGenerator) {
      mArrangementGenerator.generate(
          dynamicGenerator->getActiveClippingPrimitives());
    } else {
      mArrangementGenerator.setActiveLayer(
          world->getWorldDataGenerator()->getActiveLayer());
      mArrangementGenerator.generate(world);
    }
    updateDataProviders(*mArrangementGenerator.getWorldData());
    mWorldHasChanged = false;
  }

  for (auto& item : mMaterialRenderers) {
    auto& [renderer, dataProvider] = item;

    // Update renderer
    renderer->update(frameTime);
  }
}