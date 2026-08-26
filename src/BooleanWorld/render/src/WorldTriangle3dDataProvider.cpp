#include "WorldTriangle3dDataProvider.h"

#include <bit>
#include <cstring>

static_assert(sizeof(WorldTriangle3dDataProvider::DrawVert) == 9 * sizeof(uint32_t));

size_t WorldTriangle3dDataProvider::VertexKeyHash::operator()(
    VertexKey const& key) const noexcept {
  size_t hash = 0;
  for (auto word : key) {
    hash ^= size_t(word) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  }
  return hash;
}

WorldTriangle3dDataProvider::WorldTriangle3dDataProvider()
    : mVertexStride(sizeof(float) * 8 + sizeof(uint8_t) * 4) {
}

WorldTriangle3dDataProvider::~WorldTriangle3dDataProvider() {
  for (auto& meshData : mMeshData) {
    delete[] meshData.vertexData;
    delete[] meshData.indexData;
  }
}

void WorldTriangle3dDataProvider::getBounds(glm::vec3& bMin, glm::vec3& bMax) {
  bMin.x = bMin.y = bMin.z = -1e10f;
  bMax.x = bMax.y = bMax.z = 1e10f;
}

void WorldTriangle3dDataProvider::clear() {
  updateInternals(std::vector<uint32_t>(mMeshData.size()));
}

void WorldTriangle3dDataProvider::setMeshCount(uint32_t numMeshes) {
  mMeshData.resize(numMeshes);
  mVertexIndices.resize(numMeshes);
}

uint32_t WorldTriangle3dDataProvider::getNumMeshes() const {
  return (uint32_t)mMeshData.size();
}

WorldTriangle3dDataProvider::MeshData const& WorldTriangle3dDataProvider::getMeshData(uint32_t index) const {
  return mMeshData[index];
}

WorldTriangle3dDataProvider::DrawVert* WorldTriangle3dDataProvider::nextVertexPtr(uint32_t meshIndex) {
  auto& meshData = mMeshData[meshIndex];

  meshData.numVertices++;

  return meshData._workVert++;
}

uint32_t WorldTriangle3dDataProvider::addVertex(
    uint32_t meshIndex, DrawVert const& vertex) {
  auto key = std::bit_cast<VertexKey>(vertex);
  auto& indices = mVertexIndices[meshIndex];
  if (auto existing = indices.find(key); existing != indices.end()) {
    return existing->second;
  }

  auto& meshData = mMeshData[meshIndex];
  auto index = meshData.numVertices;
  *nextVertexPtr(meshIndex) = vertex;
  indices.emplace(key, index);
  return index;
}

void WorldTriangle3dDataProvider::addTriangle(uint32_t meshIndex, uint32_t v0, uint32_t v1, uint32_t v2) {
  auto& meshData = mMeshData[meshIndex];

  *meshData._workIndex++ = v0;
  *meshData._workIndex++ = v1;
  *meshData._workIndex++ = v2;
  meshData.numTriangles++;
}

uint32_t WorldTriangle3dDataProvider::getNumTriangles() const {
  uint32_t numTriangles{0};

  for (auto const& meshData : mMeshData) {
    numTriangles += meshData.numTriangles;
  }

  return numTriangles;
}

uint32_t WorldTriangle3dDataProvider::getNumVertices() const {
  uint32_t numVertices{0};

  for (auto const& meshData : mMeshData) {
    numVertices += meshData.numVertices;
  }

  return numVertices;
}

int8_t* WorldTriangle3dDataProvider::getVertexData(uint32_t meshIndex) const {
  auto const& meshData = mMeshData[meshIndex];

  return meshData.vertexData;
}

uint32_t WorldTriangle3dDataProvider::getVertexDataSize(uint32_t meshIndex) const {
  auto const& meshData = mMeshData[meshIndex];

  return meshData.numVertices * mVertexStride;
}

int8_t* WorldTriangle3dDataProvider::getIndexData(uint32_t meshIndex) const {
  auto const& meshData = mMeshData[meshIndex];

  return (int8_t*)meshData.indexData;
}

uint32_t WorldTriangle3dDataProvider::getNumIndices(uint32_t meshIndex) const {
  auto const& meshData = mMeshData[meshIndex];

  return meshData.numTriangles * 3;
}

uint32_t WorldTriangle3dDataProvider::getIndexWidth() const {
  return 32;
}

mpp::Colour WorldTriangle3dDataProvider::diffuse() {
  return mpp::Colour::White;
}

void WorldTriangle3dDataProvider::updateInternals(
    std::vector<uint32_t> const& numTrianglesPerMesh) {
  for (uint32_t meshIndex = 0; meshIndex < mMeshData.size(); ++meshIndex) {
    auto& meshData = mMeshData[meshIndex];
    auto numTriangles = numTrianglesPerMesh[meshIndex];
    auto numVertices = numTriangles * 3;
    auto newVertexDataSize = numVertices * mVertexStride;

    if (newVertexDataSize > meshData.vertexDataSize) {
      meshData.vertexDataSize = newVertexDataSize;

      delete[] meshData.vertexData;
      meshData.vertexData = new int8_t[meshData.vertexDataSize];
    }

    auto numIndices = numTriangles * 3;
    auto newIndexDataSize = (uint32_t)(numIndices * sizeof(uint32_t));

    if (newIndexDataSize > meshData.indexDataSize) {
      meshData.indexDataSize = newIndexDataSize;

      delete[] meshData.indexData;
      meshData.indexData = new uint32_t[numIndices];
    }

    meshData._workVert = (DrawVert*)meshData.vertexData;
    meshData._workIndex = meshData.indexData;

    meshData.numVertices = 0;
    meshData.numTriangles = 0;
    mVertexIndices[meshIndex].clear();
    mVertexIndices[meshIndex].reserve(numVertices);
  }

  setNumPrimitives(0);
}

void WorldTriangle3dDataProvider::finalizeInternals() {
  for (uint32_t meshIndex = 0; meshIndex < mMeshData.size(); ++meshIndex) {
    auto& meshData = mMeshData[meshIndex];
    auto usedSize = meshData.numVertices * mVertexStride;
    if (usedSize != meshData.vertexDataSize) {
      auto* compactVertexData = usedSize == 0 ? nullptr : new int8_t[usedSize];
      if (usedSize != 0) {
        std::memcpy(compactVertexData, meshData.vertexData, usedSize);
      }
      delete[] meshData.vertexData;
      meshData.vertexData = compactVertexData;
      meshData.vertexDataSize = usedSize;
      meshData._workVert = usedSize == 0 ? nullptr : reinterpret_cast<DrawVert*>(meshData.vertexData + usedSize);
    }

    // Vertex lookup is generation-only working state, not part of the
    // persistent renderer representation.
    mVertexIndices[meshIndex].clear();
    mVertexIndices[meshIndex].rehash(0);
  }
}
