#include <iostream>
#include <stdexcept>

#include "WorldTriangle3dDataProvider.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void destructorReleasesArrayBuffers() {
  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(2);
  provider.updateInternals(3, 1);

  for (uint32_t meshIndex = 0; meshIndex < provider.getNumMeshes(); ++meshIndex) {
    for (int vertex = 0; vertex < 3; ++vertex) {
      provider.nextVertexPtr(meshIndex);
    }
    provider.addTriangle(meshIndex, 0, 1, 2);
  }

  require(provider.getNumVertices() == 6,
          "renderer vertex buffers did not retain their vertices");
  require(provider.getNumTriangles() == 2,
          "renderer index buffers did not retain their triangles");
}

void indicesRemainValidBeyondSixteenBits() {
  constexpr uint32_t triangleCount = 21'846;
  constexpr uint32_t vertexCount = triangleCount * 3;

  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(1);
  provider.updateInternals(vertexCount, triangleCount);

  for (uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
    auto baseIndex = provider.getNumIndices(0);
    provider.nextVertexPtr(0);
    provider.nextVertexPtr(0);
    provider.nextVertexPtr(0);
    provider.addTriangle(0, baseIndex, baseIndex + 1, baseIndex + 2);
  }

  auto const& mesh = provider.getMeshData(0);
  require(provider.getIndexWidth() == 32,
          "world renderer did not advertise 32-bit indices");
  require(mesh.indexData[65'535] == 65'535,
          "world renderer corrupted the last 16-bit index");
  require(mesh.indexData[65'536] == 65'536,
          "world renderer truncated an index beyond 16 bits");
  require(mesh.indexData[vertexCount - 1] == vertexCount - 1,
          "world renderer corrupted the final large-mesh index");
}

}  // namespace

int main() {
  try {
    destructorReleasesArrayBuffers();
    indicesRemainValidBeyondSixteenBits();
    std::cout << "World triangle data provider supports 32-bit indices\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
