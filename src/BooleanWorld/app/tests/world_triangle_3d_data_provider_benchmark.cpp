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

}  // namespace

int main() {
  constexpr uint32_t meshCount = 64;
  constexpr uint32_t triangleCount = 4'096;
  constexpr uint32_t trianglesPerMesh = triangleCount / meshCount;
  constexpr uint64_t bytesPerTriangle =
      3 * (sizeof(WorldTriangle3dDataProvider::DrawVert) + sizeof(uint32_t));
  constexpr uint64_t beforeBytes =
      uint64_t(meshCount) * triangleCount * bytesPerTriangle;
  constexpr uint64_t expectedAfterBytes = triangleCount * bytesPerTriangle;

  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(meshCount);
  provider.updateInternals(std::vector<uint32_t>(meshCount, trianglesPerMesh));
  auto afterBytes = allocatedBytes(provider);

  if (afterBytes != expectedAfterBytes) {
    std::cerr << "Per-mesh renderer buffer allocation mismatch\n";
    return 1;
  }

  std::cout << std::fixed << std::setprecision(2)
            << "World renderer buffer allocation benchmark: " << meshCount
            << " meshes, " << triangleCount << " triangles\n"
            << "  before world-sized buffers: "
            << double(beforeBytes) / (1024 * 1024) << " MiB\n"
            << "  after per-mesh buffers: "
            << double(afterBytes) / (1024 * 1024) << " MiB\n";
}
