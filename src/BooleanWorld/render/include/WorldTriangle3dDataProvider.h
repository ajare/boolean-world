#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include <mpp/helper/TriangleBatchDataProvider.h>

class WorldTriangle3dDataProvider : public mpp::helper::TriangleBatch3DBufferDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte> {
  friend class WorldRenderer;

public:
  enum class TriangleOrder { Authored,
                             FrontToBack,
                             BackToFront };

  // A finite value rather than negative infinity keeps the dry marker valid
  // for every vertex format and safely below every playable world position.
  static constexpr float dryLiquidSurfaceHeight = -1e10f;

  struct DrawVert {
    float pos[3];
    float nor[3];
    float tex[2];
    uint32_t col;
    float liquidSurfaceHeight;
    // The unperturbed, canonical up-facing normal of a horizontal surface.
    // Unlike `nor`, this is never face-forwarded or normal-map perturbed.
    float surfaceUp[3];
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
  using VertexKey = std::array<uint32_t, 13>;

  struct VertexKeyHash {
    size_t operator()(VertexKey const& key) const noexcept;
  };

  uint32_t mVertexStride;

  std::vector<MeshData> mMeshData;
  std::vector<std::unordered_map<VertexKey, uint32_t, VertexKeyHash>> mVertexIndices;
  // Authored order retained lazily while the diagnostic sort is in use, so
  // disabling it restores the normal renderer path without charging the
  // default path a second full index allocation.
  std::vector<std::vector<uint32_t>> mAuthoredIndices;
  TriangleOrder mTriangleOrder{TriangleOrder::Authored};

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

  // Restores authored triangle order or sorts every material mesh by
  // triangle-centroid distance from the view. Blended liquid uses
  // BackToFront independently of opaque diagnostic ordering.
  void orderTrianglesForView(glm::vec3 const& viewPosition, TriangleOrder order);

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
