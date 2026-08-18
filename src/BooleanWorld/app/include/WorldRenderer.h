#pragma once

#include <map>
#include <vector>
#include <string>

#include <mpp/Scene.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/World.h>

#include "WorldTriangle3dDataProvider.h"
#include "WorldRenderer3d.h"

class WorldRenderer {
  typedef std::shared_ptr<WorldTriangle3dDataProvider> DataProvider;

  typedef std::shared_ptr<WorldRenderer3d> Renderer;

  typedef std::pair<Renderer, DataProvider> MaterialRenderer;

private:
  std::vector<MaterialRenderer> mMaterialRenderers;

  bool mWorldHasChanged;

  wp::Logger* mwLogger;

  // The screen-sized target the 3d world is composited from (ADR 0012).
  mpp::RenderTargetPtr mWorldTarget;

private:
  void updateDataProviders(bw::core::WorldData const& worldData);

  uint32_t addVertexToDataProvider(DataProvider dataProvider, uint32_t meshIndex, float px, float py, float pz, float nx, float ny, float nz, float u, float v, uint32_t c);

public:
  WorldRenderer(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::Logger* logger);

  virtual ~WorldRenderer();

  void setWorldChanged();

  void create(mpp::ScenePtr scene, bw::core::World const* world, mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

  // Builds the offscreen target the world is composited from. This talks to
  // OpenGL, so it belongs on the main thread - the map load post-work step.
  void createRenderTarget(mpp::RenderSystem* renderSystem);

  mpp::RenderTargetPtr const& getRenderTarget() const;

  // Hands the target to a caller that outlives this renderer, so map unload can
  // release it from its main-thread post-work step rather than from the
  // threadable pre-work that tears the renderer itself down.
  mpp::RenderTargetPtr detachRenderTarget();

  void update(bw::core::World* world, bw::core::WorldData const& worldData, float frameTime);
};
