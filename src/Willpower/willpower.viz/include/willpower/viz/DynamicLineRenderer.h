#pragma once

#include <mpp/helper/LineBatchRenderer.h>
#include <mpp/helper/LineBatchDataProvider.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/DynamicRenderer.h"
#include "willpower/viz/DynamicLineRenderParams.h"

namespace WP_NAMESPACE {
namespace viz {

class WP_VIZ_API DynamicLineRenderer : public DynamicRenderer {
  std::shared_ptr<mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> mDataProvider;

  std::shared_ptr<mpp::helper::LineBatchRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> mRenderer;

  mpp::helper::LineBatchRendererParams mLineParams;

private:
  RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params) override;

  mpp::SceneModel2dPtr createSceneModelInScene(mpp::ScenePtr scene, int renderOrder) override;

  void removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) override;

public:
  DynamicLineRenderer(std::string const& name, std::shared_ptr<mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> dataProvider, mpp::helper::LineBatchRendererParams const& params, mpp::ResourceManager* renderResourceMgr);

  void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

  void update(BoundingBox const& viewBounds, float frameTime) override;
};

}  // namespace viz
}  // namespace WP_NAMESPACE
