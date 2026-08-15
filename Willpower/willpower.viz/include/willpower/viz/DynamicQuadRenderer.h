#pragma once

#include <mpp/helper/QuadBatchRenderer.h>
#include <mpp/helper/QuadBatchDataProvider.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/DynamicRenderer.h"
#include "willpower/viz/DynamicQuadRenderParams.h"

#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE
{
	namespace viz
	{

		enum class RotationOptions
		{
			None,
			TexCoordsByAngle,
			VerticesByAngle,
			TexCoordsByDirection,
			VerticesByDirection,
		};

		struct QuadRendererOptions
		{
			RotationOptions rotation;
			bool fixedTexCoords;
			size_t width, height;
			mpp::ResourcePtr texture;
			size_t indexWidth;

		};

		template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
		class DynamicQuadRenderer : public DynamicRenderer
		{
			std::shared_ptr<mpp::helper::QuadBatchDataProvider<PosType, TexType, ColType>> mDataProvider;

			std::shared_ptr<mpp::helper::QuadBatchRenderer<PosType, TexType, ColType>> mRenderer;

			QuadRendererOptions mOptions;

		private:

			RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params)
			{
				return new DynamicQuadRenderParams(params);
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

			DynamicQuadRenderer(std::string const& name, QuadRendererOptions const& options, std::shared_ptr<mpp::helper::QuadBatchDataProvider<PosType, TexType, ColType>> dataProvider, mpp::ResourceManager* renderResourceMgr)
				: DynamicRenderer(name, "Quad", renderResourceMgr)
				, mDataProvider(dataProvider)
				, mOptions(options)
			{
			}

			void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override
			{
				mpp::QuadBatchOptions::PrimitiveOptions primitiveOptions;
				mpp::QuadBatchOptions::RotationOptions rotationOptions;

				switch (mOptions.rotation)
				{
				case RotationOptions::None:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::None;
					break;
				case RotationOptions::TexCoordsByAngle:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Angle;
					break;
				case RotationOptions::VerticesByAngle:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Triangles;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Angle;
					break;
				case RotationOptions::TexCoordsByDirection:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Direction;
					break;
				case RotationOptions::VerticesByDirection:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Triangles;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Direction;
					break;
				default:
					throw Exception("Unknown RotationOptions value.");
				}

				mpp::helper::QuadBatchRendererParams quadParams(
					primitiveOptions,
					rotationOptions,
					mOptions.fixedTexCoords,            // fixed texcoords
					false,                              // colour
					true,                               // use vertex colours
					false,                              // don't use diffuse colour
					mOptions.width,                     // width
					mOptions.height,                    // height
					mOptions.width == mOptions.height,  // square
					mOptions.indexWidth,                // index size (ie max of 2^16 or 2^32)
					mOptions.texture);

				mRenderer = std::make_shared<mpp::helper::QuadBatchRenderer<PosType, TexType, ColType>>(
					getName() + "_Renderer",
					quadParams,
					mDataProvider,
					renderSystem,
					resourceMgr);

				mRenderer->create();
			}

			void update(BoundingBox const& viewBounds, float frameTime) override
			{
				WP_UNUSED(viewBounds);
				WP_UNUSED(frameTime);

				mRenderer->update(mDataProvider->getNumPrimitives());
			}
		};

		template<typename PosType, typename TexType>
		class DynamicQuadRenderer<PosType, TexType, mpp::mesh::DataTypeNone> : public DynamicRenderer
		{
			std::shared_ptr<mpp::helper::QuadBatchDataProvider<PosType, TexType>> mDataProvider;

			std::shared_ptr<mpp::helper::QuadBatchRenderer<PosType, TexType>> mRenderer;

			QuadRendererOptions mOptions;

		private:

			RenderParams* createRenderParams(std::shared_ptr<mpp::ModelRenderParams> params)
			{
				return new DynamicQuadRenderParams(params);
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

			DynamicQuadRenderer(std::string const& name, QuadRendererOptions const& options, std::shared_ptr<mpp::helper::QuadBatchDataProvider<PosType, TexType>> dataProvider, mpp::ResourceManager* renderResourceMgr)
				: DynamicRenderer(name, "Quad", renderResourceMgr)
				, mDataProvider(dataProvider)
				, mOptions(options)
			{
			}

			void build(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override
			{
				mpp::QuadBatchOptions::PrimitiveOptions primitiveOptions;
				mpp::QuadBatchOptions::RotationOptions rotationOptions;

				switch (mOptions.rotation)
				{
				case RotationOptions::None:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::None;
					break;
				case RotationOptions::TexCoordsByAngle:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Angle;
					break;
				case RotationOptions::VerticesByAngle:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Triangles;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Angle;
					break;
				case RotationOptions::TexCoordsByDirection:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Auto;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Direction;
					break;
				case RotationOptions::VerticesByDirection:
					primitiveOptions = mpp::QuadBatchOptions::PrimitiveOptions::Triangles;
					rotationOptions = mpp::QuadBatchOptions::RotationOptions::Direction;
					break;
				default:
					throw Exception("Unknown RotationOptions value.");
				}

				mpp::helper::QuadBatchRendererParams quadParams(
					primitiveOptions,
					rotationOptions,
					mOptions.fixedTexCoords,            // fixed texcoords
					true,                               // fixed colour (no colour, in fact)
					false,                              // don't use vertex colours
					true,                               // use diffuse colour
					mOptions.width,                     // width
					mOptions.height,                    // height
					mOptions.width == mOptions.height,  // square
					mOptions.indexWidth,                // index size (ie max of 2^16 or 2^32)
					mOptions.texture);

				mRenderer = std::make_shared<mpp::helper::QuadBatchRenderer<PosType, TexType>>(
					getName() + "_Renderer",
					quadParams,
					mDataProvider,
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

