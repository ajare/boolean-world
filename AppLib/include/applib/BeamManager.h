#pragma once

#include <vector>

#include <willpower/viz/DynamicLineRenderer.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/firepower/MeshCollisionManager.h>

#include "Battery.h"
#include "Beam.h"
#include "BeamInstance.h"
#include "BeamRenderDataProvider.h"
#include "Entity.h"

namespace applib
{

	class APPLIB_API BeamManager
	{
		Battery<BeamInstance> mBattery;

		wp::application::resourcesystem::ResourcePtr mGameResource;

		std::shared_ptr<BeamRenderDataProvider> mDataProvider;

		wp::viz::DynamicLineRenderer* mRenderer;

		wp::firepower::MeshCollisionManager* mwMeshCollisionMgr;

	private:

		static bool updateBeam(BeamInstance& beamInstance, void* userObj, float frameTime);

	public:

		BeamManager(wp::application::resourcesystem::ResourcePtr gameResource, wp::firepower::MeshCollisionManager* meshCollisionMgr, size_t initialCapacity);

		virtual ~BeamManager();

		void setupRenderer(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, mpp::ScenePtr scene, int renderOrder);

		int addBeam(int type, BeamData const& data, Entity* owner);

		void removeBeam(int beamId);

		void update(wp::BoundingBox const& viewBounds, float frameTime);
	};

} // applib