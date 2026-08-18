#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include <mpp/helper/TriangleBatchDataProvider.h>

class WorldTriangle3dDataProvider : public mpp::helper::TriangleBatch3DBufferDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte> {
  friend class WorldRenderer;

public:
  struct DrawVert {
    float pos[3];
    float nor[3];
    float tex[2];
    uint32_t col;
  };

  struct MeshData {
    uint32_t numVertices{0};
    uint32_t numTriangles{0};
    uint32_t vertexDataSize{0};
    uint32_t indexDataSize{0};
    int8_t* vertexData{nullptr};
    uint32_t* indexData{nullptr};
    DrawVert* _workVert{nullptr};
    uint32_t* _workIndex{nullptr};
  };

private:
  using VertexKey = std::array<uint32_t, 9>;

  struct VertexKeyHash {
    size_t operator()(VertexKey const& key) const noexcept;
  };

  uint32_t mVertexStride;

  std::vector<MeshData> mMeshData;
  std::vector<std::unordered_map<VertexKey, uint32_t, VertexKeyHash>> mVertexIndices;

public:
  WorldTriangle3dDataProvider();

  ~WorldTriangle3dDataProvider();

  void getBounds(glm::vec3& bMin, glm::vec3& bMax) override;

  void clear();

  void setMeshCount(uint32_t numMeshes);

  uint32_t getNumMeshes() const;

  MeshData const& getMeshData(uint32_t index) const;

  DrawVert* nextVertexPtr(uint32_t meshIndex);

  uint32_t addVertex(uint32_t meshIndex, DrawVert const& vertex);

  void updateInternals(std::vector<uint32_t> const& numTrianglesPerMesh);

  void finalizeInternals();

  void addTriangle(uint32_t meshIndex, uint32_t v0, uint32_t v1, uint32_t v2);

  uint32_t getNumTriangles() const;

  uint32_t getNumVertices() const override;

  int8_t* getVertexData(uint32_t meshIndex) const override;

  uint32_t getVertexDataSize(uint32_t meshIndex) const override;

  int8_t* getIndexData(uint32_t meshIndex) const override;

  uint32_t getNumIndices(uint32_t meshIndex) const override;

  uint32_t getIndexWidth() const override;

  mpp::Colour diffuse() override;
};
