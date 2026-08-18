#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "WorldTriangle3dDataProvider.h"

namespace {

uint64_t allocatedBytes(WorldTriangle3dDataProvider const& provider) {
  uint64_t result = 0;
  for (uint32_t meshIndex = 0; meshIndex < provider.getNumMeshes(); ++meshIndex) {
    auto const& mesh = provider.getMeshData(meshIndex);
    result += mesh.vertexDataSize + mesh.indexDataSize;
  }
  return result;
}

uint64_t uploadBytes(WorldTriangle3dDataProvider const& provider) {
  uint64_t result = 0;
  for (uint32_t meshIndex = 0; meshIndex < provider.getNumMeshes(); ++meshIndex) {
    result += provider.getVertexDataSize(meshIndex);
    result += uint64_t(provider.getNumIndices(meshIndex)) * sizeof(uint32_t);
  }
  return result;
}

WorldTriangle3dDataProvider::DrawVert floorVertex(float x, float y) {
  return {{x, 0.0f, y}, {0.0f, 1.0f, 0.0f}, {x / 64.0f, y / 64.0f}, 0xffffffffu};
}

}  // namespace

int main() {
  constexpr uint32_t gridWidth = 64;
  constexpr uint32_t meshCount = 4;
  constexpr uint32_t triangleCount = gridWidth * gridWidth * 2;
  constexpr uint32_t trianglesPerMesh = triangleCount / meshCount;
  constexpr uint64_t vertexSize = sizeof(WorldTriangle3dDataProvider::DrawVert);
  constexpr uint64_t currentSequentialIndexedBytes =
      uint64_t(triangleCount) * 3 * (vertexSize + sizeof(uint32_t));
  constexpr uint64_t nonIndexedBytes =
      uint64_t(triangleCount) * 3 * vertexSize;

  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(meshCount);
  provider.updateInternals(std::vector<uint32_t>(meshCount, trianglesPerMesh));

  for (uint32_t y = 0; y < gridWidth; ++y) {
    for (uint32_t x = 0; x < gridWidth; ++x) {
      auto meshIndex = x / (gridWidth / meshCount);
      auto v00 = provider.addVertex(meshIndex, floorVertex(float(x), float(y)));
      auto v10 = provider.addVertex(meshIndex, floorVertex(float(x + 1), float(y)));
      auto v11 = provider.addVertex(meshIndex, floorVertex(float(x + 1), float(y + 1)));
      auto v01 = provider.addVertex(meshIndex, floorVertex(float(x), float(y + 1)));
      provider.addTriangle(meshIndex, v00, v10, v11);
      provider.addTriangle(meshIndex, v11, v01, v00);
    }
  }
  provider.finalizeInternals();

  auto safeReuseUploadBytes = uploadBytes(provider);
  auto safeReuseAllocatedBytes = allocatedBytes(provider);
  if (safeReuseUploadBytes >= nonIndexedBytes ||
      safeReuseAllocatedBytes != safeReuseUploadBytes) {
    std::cerr << "Safe world-renderer vertex reuse was not beneficial\n";
    return 1;
  }

  std::cout << std::fixed << std::setprecision(2)
            << "World renderer representation benchmark: " << meshCount
            << " material meshes, " << triangleCount << " floor triangles\n"
            << "  current sequential indexed: "
            << double(currentSequentialIndexedBytes) / (1024 * 1024) << " MiB\n"
            << "  non-indexed: "
            << double(nonIndexedBytes) / (1024 * 1024) << " MiB\n"
            << "  indexed safe vertex reuse: "
            << double(safeReuseUploadBytes) / (1024 * 1024) << " MiB\n";
}
