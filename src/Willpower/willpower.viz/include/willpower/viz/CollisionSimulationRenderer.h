#pragma once

#include <memory>

#include "willpower/collide/Simulation.h"

#include "willpower/viz/Platform.h"
#include "willpower/viz/StaticRenderer.h"
#include "willpower/viz/CollisionSimulationRenderParams.h"

namespace WP_NAMESPACE {
namespace viz {

class WP_VIZ_API CollisionSimulationRenderer : public StaticRenderer {
  collide::Simulation* mwSimulation;

private:
  void getExtents(Vector2& minExtent, Vector2& maxExtent) override;

  // MeshSpecifications
  void createMeshSpecifications() override;

  // Materials
  void createMaterials(mpp::ResourceManager* resourceMgr) override;

  // Meshes
  void createMeshes(mpp::ProgrammaticModelStream* stream, mpp::ResourceManager* resourceMgr) override;

  // RenderParams
  RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params) override;

public:
  CollisionSimulationRenderer(std::string const& name, collide::Simulation* simulation, size_t indexWidth, mpp::ResourceManager* renderResourceMgr);

  void updateRenderParams() override;
};

}  // namespace viz
}  // namespace WP_NAMESPACE
