#pragma once

#include <memory>
#include <set>

#include <mpp/ResourceManager.h>
#include <mpp/SceneModel2d.h>
#include <mpp/ProgrammaticModelStream.h>
#include <mpp/mesh/MeshSpecification.h>

#include "willpower/common/Vector2.h"
#include "willpower/common/AccelerationGrid.h"

#include "willpower/viz/Platform.h"
#include "willpower/viz/Renderer.h"

namespace WP_NAMESPACE {
namespace viz {

class WP_VIZ_API StaticRenderer : public Renderer {
public:
  struct GridOptions {
    enum class CellStrategy {
      Intersects,
      CentreInside
    };

    bool useGrid{false};
    float cellSize{256.0f};
    float paddingPct{0.0f};
    CellStrategy strategy{CellStrategy::Intersects};
  };

private:
  mpp::ResourcePtr mModel;

  std::vector<mpp::ResourcePtr> mMaterials;

  GridOptions mGridOptions;

  bool mUseIncludeMasking, mUseExcludeMasking;

  std::set<uint32_t> mGridIncludeMask, mGridExcludeMask;

protected:
  std::shared_ptr<AccelerationGrid> mLookupGrid;

  size_t mIndexWidth;

  std::map<std::string, std::string> mMaterialNames;

  std::map<std::string, mpp::mesh::MeshSpecification> mMeshSpecifications;

private:
  void createLookupGrid();

  mpp::SceneModel2dPtr createSceneModelInScene(mpp::ScenePtr scene, int renderOrder) override;

  void removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) override;

  virtual void getExtents(Vector2& minExtent, Vector2& maxExtent) = 0;

  // Mesh specifications
  virtual void createMeshSpecifications() = 0;

  // Materials
  virtual void createMaterials(mpp::ResourceManager* resourceMgr) = 0;

  // Meshes
  virtual void createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) = 0;

protected:
  void addMaterialResource(std::string const& lookupName, mpp::ResourcePtr resource);

  bool usingLookupGrid() const;

  void addVertexData(mpp::mesh::VertexData* vertexData, Vector2 const& pos, Vector2 const& tex, float colour[4]);

  void addVertexData(mpp::mesh::VertexData* vertexData, Vector2 const& pos0, Vector2 const& pos1, Vector2 const& tex0, Vector2 const& tex1, float colour[4]);

  GridOptions::CellStrategy getGridCellStrategy() const;

public:
  StaticRenderer(std::string const& name, std::string const& type, GridOptions const& gridOptions, size_t indexWidth, mpp::ResourceManager* renderResourceMgr);

  ~StaticRenderer();

  std::shared_ptr<AccelerationGrid> getLookupGrid();

  void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

  void useGridIncludeMasking(bool use);

  bool usingGridIncludeMasking() const;

  void useGridExcludeMasking(bool use);

  bool usingGridExcludeMasking() const;

  void clearGridIncludeMask();

  void clearGridExcludeMask();

  void addToGridIncludeMask(uint32_t id);

  void addToGridExcludeMask(uint32_t id);

  bool gridIdInIncludeMask(uint32_t id) const;

  bool gridIdInExcludeMask(uint32_t id) const;
};

}  // namespace viz
}  // namespace WP_NAMESPACE

#pragma once
