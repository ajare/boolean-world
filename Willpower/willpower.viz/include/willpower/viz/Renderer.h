#pragma once
#include <string>

#include <mpp/ResourceWrangler.h>
#include <mpp/ResourceManager.h>
#include <mpp/Scene.h>
#include <mpp/SceneModel2d.h>
#include <mpp/ModelRenderParams.h>

#include "willpower/common/Vector2.h"
#include "willpower/common/BoundingBox.h"

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{

		class WP_VIZ_API Renderer : public mpp::ResourceWrangler
		{
			std::string mName;

			std::string mType;

			std::shared_ptr<RenderParams> mParams;

			bool mInScene;

		protected:

			mpp::SceneModel2dPtr mSceneModel;

			mpp::ResourceManager* mRenderResourceMgr;

		private:

			virtual RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params) = 0;

			virtual mpp::SceneModel2dPtr createSceneModelInScene(mpp::ScenePtr scene, int renderOrder) = 0;

			virtual void removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) = 0;

		protected:

			void setRenderParams(std::shared_ptr<mpp::ModelRenderParams> params);

			virtual void onAddedToScene();

		public:

			Renderer(std::string const& name, std::string const& type, mpp::ResourceManager* renderResourceMgr);

			virtual ~Renderer() = default;

			std::string const& getName() const;

			std::string const& getType() const;

			std::shared_ptr<mpp::ModelRenderParams> getModelRenderParams();

			std::shared_ptr<RenderParams> getParams();

			virtual void updateRenderParams();

			virtual void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) = 0;

			void addToScene(mpp::ScenePtr scene, int renderOrder);

			void removeFromScene(mpp::ScenePtr scene);

			virtual void update(BoundingBox const& viewBounds, float frameTime);

			void setVisible(bool visible);

			bool isVisible() const;

			void setOrigin(wp::Vector2 const& origin);

			wp::Vector2 getOffset() const;

			void setOffset(wp::Vector2 const& offset);

			wp::Vector2 getOrigin() const;

			void setAngle(float angle);

			float getAngle() const;

			void setOrbitAngle(float angle);

			float getOrbitAngle() const;

			void setScale(wp::Vector2 const& scale);

			wp::Vector2 getScale() const;

			void setScreenSpace(bool screenSpace);

			bool inScreenSpace() const;
		};

	} // viz
} // WP_NAMESPACE

#pragma once
