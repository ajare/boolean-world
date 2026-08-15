#include "BeamManager.h"
#include "Game.h"
#include "ModelInstance.h"
#include "Entity.h"
#include "Exceptions.h"

namespace applib {
using namespace std;
using namespace wp;

BeamManager::BeamManager(application::resourcesystem::ResourcePtr gameResource, firepower::MeshCollisionManager* meshCollisionMgr, size_t initialCapacity)
    : mBattery(initialCapacity, BeamManager::updateBeam), mGameResource(gameResource), mRenderer(nullptr), mwMeshCollisionMgr(meshCollisionMgr) {
}

BeamManager::~BeamManager() {
  delete mRenderer;
}

void BeamManager::setupRenderer(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, mpp::ScenePtr scene, int renderOrder) {
  mDataProvider = make_shared<BeamRenderDataProvider>(&mBattery);

  mpp::helper::LineBatchRendererParams params{true, true, false};

  mRenderer = new viz::DynamicLineRenderer(
      "Beams",
      mDataProvider,
      params,
      renderResourceMgr);

  mRenderer->build(renderSystem, renderResourceMgr);
  mRenderer->addToScene(scene, renderOrder);
}

bool BeamManager::updateBeam(BeamInstance& beamInstance, void* userObj, float frameTime) {
  if (beamInstance.alive) {
    auto mgr = static_cast<BeamManager*>(userObj);
    auto const& beamData = beamInstance.data;

    auto const& beamEmitter = ModelInstance::entityHandler()->getEntityComponent<BeamEmitter>(*beamInstance.owner);

    auto beamShards = mgr->mwMeshCollisionMgr ? mgr->mwMeshCollisionMgr->checkBeam(
                                                    beamEmitter.anchorPos,
                                                    beamEmitter.anchorAngle,
                                                    beamData.width,
                                                    beamData.length)
                                              : firepower::MeshCollisionManager::getFullBeam(
                                                    beamEmitter.anchorPos,
                                                    beamEmitter.anchorAngle,
                                                    beamData.width,
                                                    beamData.length);

    mgr->mDataProvider->addData(beamShards);
  }

  return beamInstance.alive;
}

int BeamManager::addBeam(int type, BeamData const& data, Entity* owner) {
  int id = Beam::msBeamIdGen++;

  BeamInstance bi{
      {id,
       type},
      data,
      owner,
      true};

  mBattery.addObject(bi);
  return id;
}

void BeamManager::removeBeam(int beamId) {
  // Mark beam with this id to be deleted
  for (uint32_t i = 0; i < mBattery.getCount(); ++i) {
    auto& beamInstance = mBattery.getObject(i);
    if (beamInstance.beam.id == beamId) {
      beamInstance.alive = false;
      return;
    }
  }
}

void BeamManager::update(BoundingBox const& viewBounds, float frameTime) {
  mDataProvider->clearData();

  mBattery.update(this, frameTime);

  mDataProvider->update(frameTime);
  mRenderer->update(viewBounds, frameTime);
}

}  // namespace applib