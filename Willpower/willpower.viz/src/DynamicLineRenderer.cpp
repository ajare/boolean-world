#include "willpower/viz/DynamicLineRenderer.h"

namespace WP_NAMESPACE {
namespace viz {

using namespace std;
using namespace wp;

DynamicLineRenderer::DynamicLineRenderer(string const& name, shared_ptr<mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>> dataProvider, mpp::helper::LineBatchRendererParams const& params, mpp::ResourceManager* renderResourceMgr)
    : DynamicRenderer(name, "Line", renderResourceMgr), mDataProvider(dataProvider), mLineParams(params) {
}

void DynamicLineRenderer::build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  mpp::helper::LineBatchRendererParams lineParams{
      true,
      true,
      false};

  mRenderer = make_shared<mpp::helper::LineBatchRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>>(
      getName() + "_Renderer",
      lineParams,
      mDataProvider,
      renderSystem,
      resourceMgr);

  mRenderer->create();
}

void DynamicLineRenderer::update(BoundingBox const& viewBounds, float frameTime) {
  WP_UNUSED(viewBounds);
  WP_UNUSED(frameTime);

  mRenderer->update();
}

RenderParams* DynamicLineRenderer::createRenderParams(shared_ptr<mpp::ModelRenderParams> params) {
  return new DynamicLineRenderParams(params);
}

mpp::SceneModel2dPtr DynamicLineRenderer::createSceneModelInScene(mpp::ScenePtr scene, int renderOrder) {
  return scene->add2dBatch(mDataProvider, mRenderer, renderOrder);
}

void DynamicLineRenderer::removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) {
  scene->remove2dBatch(sceneModel);
}

}  // namespace viz
}  // namespace WP_NAMESPACE
