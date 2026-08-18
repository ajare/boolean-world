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
  provider.updateInternals({1, 1});

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

void buffersAreSizedPerMesh() {
  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(3);
  provider.updateInternals({2, 1, 0});

  auto const& first = provider.getMeshData(0);
  auto const& second = provider.getMeshData(1);
  auto const& unused = provider.getMeshData(2);
  auto vertexSize = sizeof(WorldTriangle3dDataProvider::DrawVert);

  require(first.vertexDataSize == 6 * vertexSize,
          "first mesh vertex buffer was not sized for its triangles");
  require(first.indexDataSize == 6 * sizeof(uint32_t),
          "first mesh index buffer was not sized for its triangles");
  require(second.vertexDataSize == 3 * vertexSize,
          "second mesh vertex buffer was not sized for its triangles");
  require(second.indexDataSize == 3 * sizeof(uint32_t),
          "second mesh index buffer was not sized for its triangles");
  require(unused.vertexDataSize == 0 && unused.indexDataSize == 0,
          "unused mesh allocated renderer buffers");
}

WorldTriangle3dDataProvider::DrawVert vertex(
    float x, float y, float z, float nx, float ny, float nz,
    float u, float v, uint32_t colour) {
  return {{x, y, z}, {nx, ny, nz}, {u, v}, colour};
}

void safelyReusesVerticesWithinEachMaterialMesh() {
  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(3);
  provider.updateInternals({4, 3, 2});

  // Two floor triangles share their diagonal. A ceiling with the same position
  // remains distinct because its normal and material mesh differ.
  auto f0 = provider.addVertex(0, vertex(0, 0, 0, 0, 1, 0, 0, 0, 0x11));
  auto f1 = provider.addVertex(0, vertex(1, 0, 0, 0, 1, 0, 1, 0, 0x11));
  auto f2 = provider.addVertex(0, vertex(1, 0, 1, 0, 1, 0, 1, 1, 0x11));
  auto f3 = provider.addVertex(0, vertex(0, 0, 1, 0, 1, 0, 0, 1, 0x11));
  provider.addTriangle(0, f0, f1, f2);
  provider.addTriangle(0, f2, f3, f0);
  require(provider.addVertex(0, vertex(0, 0, 0, 0, 1, 0, 0, 0, 0x11)) == f0,
          "identical floor vertex was not reused");
  require(provider.addVertex(0, vertex(0, 0, 0, 0, -1, 0, 0, 0, 0x11)) != f0,
          "vertex with a different normal was incorrectly reused");

  auto c0 = provider.addVertex(1, vertex(0, 3, 0, 0, -1, 0, 0, 0, 0x22));
  auto c1 = provider.addVertex(1, vertex(1, 3, 1, 0, -1, 0, 1, 1, 0x22));
  auto c2 = provider.addVertex(1, vertex(1, 3, 0, 0, -1, 0, 1, 0, 0x22));
  provider.addTriangle(1, c0, c1, c2);

  // Border and step walls both use four corners for two triangles. Different
  // normals, heights, UVs, colours, or material meshes cannot alias.
  for (uint32_t meshIndex : {1u, 2u}) {
    auto colour = meshIndex == 1 ? 0x33u : 0x44u;
    auto z = meshIndex == 1 ? 2.0f : 1.0f;
    auto b0 = provider.addVertex(meshIndex, vertex(0, 0, 0, 0, 0, 1, 0, 0, colour));
    auto b1 = provider.addVertex(meshIndex, vertex(1, 0, 0, 0, 0, 1, 1, 0, colour));
    auto t1 = provider.addVertex(meshIndex, vertex(1, z, 0, 0, 0, 1, 1, 1, colour));
    auto t0 = provider.addVertex(meshIndex, vertex(0, z, 0, 0, 0, 1, 0, 1, colour));
    provider.addTriangle(meshIndex, b0, b1, t1);
    provider.addTriangle(meshIndex, t1, t0, b0);
  }

  require(provider.getNumMeshes() == 3,
          "material mesh count changed while reusing vertices");
  require(provider.getMeshData(0).numTriangles == 2 &&
              provider.getMeshData(1).numTriangles == 3 &&
              provider.getMeshData(2).numTriangles == 2,
          "floor, ceiling, border, or step triangles crossed material partitions");
  for (uint32_t meshIndex = 0; meshIndex < provider.getNumMeshes(); ++meshIndex) {
    auto const& mesh = provider.getMeshData(meshIndex);
    for (uint32_t i = 0; i < mesh.numTriangles * 3; ++i) {
      require(mesh.indexData[i] < mesh.numVertices,
              "reused renderer index is outside its material vertex range");
    }
  }
  auto const* floorVertices = reinterpret_cast<WorldTriangle3dDataProvider::DrawVert const*>(
      provider.getVertexData(0));
  require(floorVertices[0].nor[1] == 1.0f &&
              floorVertices[4].nor[1] == -1.0f,
          "renderer vertex normals changed during safe reuse");

  provider.finalizeInternals();
  require(provider.getMeshData(0).vertexDataSize ==
              provider.getMeshData(0).numVertices * sizeof(WorldTriangle3dDataProvider::DrawVert),
          "final renderer allocation retained duplicate-vertex capacity");
}

void indicesRemainValidBeyondSixteenBits() {
  constexpr uint32_t triangleCount = 21'846;
  constexpr uint32_t vertexCount = triangleCount * 3;

  WorldTriangle3dDataProvider provider;
  provider.setMeshCount(1);
  provider.updateInternals({triangleCount});

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
    buffersAreSizedPerMesh();
    safelyReusesVerticesWithinEachMaterialMesh();
    indicesRemainValidBeyondSixteenBits();
    std::cout << "World triangle data provider safely reuses material vertices with 32-bit indices\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
