#pragma once

#include <mpp/helper/TriangleBatchRenderer.h>
#include <mpp/helper/TriangleBatchDataProvider.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/DynamicRenderer.h"
#include "willpower/viz/DynamicTriangleRenderParams.h"

#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE
{
	namespace viz
	{

		struct TriangleRendererOptions
		{
			bool useMaterialNotTexture;
			bool fixedTextureData;
			bool fixedColourData;
			bool useDiffuse;
			bool indexedVertices;
			mpp::ResourcePtr texture;
		};

		template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
		class DynamicTriangleRenderer : public DynamicRenderer
		{
			std::shared_ptr<mpp::helper::TriangleBatch2DDataProvider<PosType, TexType, ColType>> mDataProvider;

			std::shared_ptr<mpp::helper::TriangleBatch2DRenderer<PosType, TexType, ColType>> mRenderer;

			TriangleRendererOptions mOptions;

		private:

			RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params)
			{
				return new DynamicTriangleRenderParams(params);
			}

			mpp::SceneModel2dPtr createSceneModelInScene(mpp::ScenePtr scene, int renderOrder)
			{
				return scene->add2dBatch(mDataProvider, mRenderer, renderOrder);
			}

			void removeSceneModelFromScene(mpp::ScenePtr scene, mpp::SceneModel2dPtr sceneModel) override
			{
				scene->remove2dBatch(sceneModel);
			}

		public:

			DynamicTriangleRenderer(std::string const& name, TriangleRendererOptions const& options, std::shared_ptr<mpp::helper::TriangleBatch2DDataProvider<PosType, TexType, ColType>> dataProvider, mpp::ResourceManager* renderResourceMgr)
				: DynamicRenderer(name, "Triangle", renderResourceMgr)
				, mDataProvider(dataProvider)
				, mOptions(options)
			{
			}

			void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override
			{
				mpp::helper::TriangleBatchRendererParams triangleParams(
					mOptions.useMaterialNotTexture,
					mOptions.fixedTextureData,
					mOptions.fixedColourData,
					mOptions.useDiffuse,
					mOptions.indexedVertices
				);

				mRenderer = std::make_shared<mpp::helper::TriangleBatch2DRenderer<PosType, TexType, ColType>>(
					getName() + "_Renderer",
					triangleParams,
					mDataProvider,
					mOptions.texture,
					renderSystem,
					resourceMgr);

				mRenderer->create();
			}

			void update(BoundingBox const& viewBounds, float frameTime) override
			{
				WP_UNUSED(viewBounds);
				WP_UNUSED(frameTime);

				mRenderer->update();
			}
		};

	} // viz
} // WP_NAMESPACE

