#pragma once

#include <memory>

#include "willpower/geometry/Mesh.h"

#include "willpower/viz/Platform.h"
#include "willpower/viz/StaticRenderer.h"
#include "willpower/viz/GeometryMeshRenderParams.h"

namespace WP_NAMESPACE {
namespace viz {

class WP_VIZ_API GeometryMeshRenderer : public StaticRenderer {
protected:
  struct BackgroundMeshData {
    mpp::ResourcePtr material{nullptr};
    uint32_t numVertices{0};
    size_t meshId{~0u};
    std::vector<float> vertices;
    mpp::mesh::VertexData* vertexData{nullptr};
  };

protected:
  std::shared_ptr<geometry::Mesh> mMesh;

  std::map<std::string, BackgroundMeshData> mBackgroundMeshes;

  std::map<uint32_t, std::string> mBackgroundMeshLookup;

private:
  void getExtents(Vector2& minExtent, Vector2& maxExtent) override;

  // MeshSpecifications
  void createMeshSpecifications() override;

  // Materials
  void createMaterials(mpp::ResourceManager* resourceMgr) override;

  std::vector<std::string> getPolygonTextures(uint32_t polygonIndex, uint32_t numTextures) const;

  std::string getPolygonMaterialLookupKey(uint32_t polygonIndex) const;

  // Meshes
  void createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) override;

  // RenderParams
  RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params) override;

  // Hooks
  virtual std::string getPolygonMeshNamePrefix(uint32_t polygonIndex) const;

protected:
  void onAddedToScene() override;

  std::string getPolygonMaterialName(uint32_t polygonIndex) const;

public:
  GeometryMeshRenderer(std::string const& name, std::shared_ptr<geometry::Mesh> mesh, GridOptions const& gridOptions, size_t indexWidth, mpp::ResourceManager* renderResourceMgr);

  void updateRenderParams() override;

  void update(BoundingBox const& viewBounds, float frameTime) override;
};

}  // namespace viz
}  // namespace WP_NAMESPACE
