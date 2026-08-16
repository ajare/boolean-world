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

}  // namespace

int main() {
  try {
    destructorReleasesArrayBuffers();
    std::cout << "World triangle data provider releases array buffers\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
