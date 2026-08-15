#pragma once

#include <vector>

#include <willpower/viz/DynamicQuadRenderer.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/firepower/MeshCollisionManager.h>

#include "Battery.h"
#include "Bullet.h"
#include "BulletRenderDataProvider.h"

namespace applib
{

	class APPLIB_API BulletManager
	{
		Battery<Bullet> mBattery;

		wp::application::resourcesystem::ResourcePtr mResource, mGameResource;
		
		std::shared_ptr<BulletRenderDataProvider> mDataProvider;

		wp::viz::DynamicQuadRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat>* mRenderer;

		wp::firepower::MeshCollisionManager* mwMeshCollisionMgr;

	private:

		static bool updateBullet(Bullet& bullet, void* userObj, float frameTime);

		static bool destroyBullet(Bullet& bullet);

	public:

		BulletManager(wp::application::resourcesystem::ResourcePtr resource, wp::application::resourcesystem::ResourcePtr gameResource, wp::firepower::MeshCollisionManager* meshCollisionMgr, size_t initialCapacity);

		virtual ~BulletManager();

		void setupRenderer(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, mpp::ScenePtr scene, int renderOrder);

		void addBullet(int type, BulletParams const& params, wp::Vector2 const& pos, wp::Vector2 dir);

		void update(wp::BoundingBox const& viewBounds, float frameTime);
	};

} // applib