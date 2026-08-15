#pragma once

#include <vector>

#include <mpp/TriangleBatch.h>
#include <mpp/TriangleBatchSpecification.h>

#include "willpower/common/TriangleBatchRenderable.h"

#include "willpower/application/resourcesystem/ResourceManager.h"
#include "willpower/application/resourcesystem/ImageResource.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Beam.h"
#include "willpower/firepower/BeamResource.h"
#include "willpower/firepower/BeamVertexDataBuilder.h"
#include "willpower/firepower/MeshCollisionManager.h"

#include "willpower/viz/TriangleBatchRenderer_old.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{

		template<typename PosType, typename TexType, typename ColType, int ColSize>
		class BeamManager
		{
			struct BeamType
			{
				Beam::SectionOptions sectionOptions;
				Beam::TexCoordOptions texCoordOptions;
				int headOffset, bodyOffset, baseOffset;
				int imageHeight;
			};

		private:

			std::vector<Beam> mBeams;

			int mNumBeams, mNumShards;

			MeshCollisionManager* mwMeshCollisionMgr;

			// Types
			std::vector<BeamType> mBeamTypes;

			std::map<std::string, int> mTypeNames;

			// Rendering
			mpp::TriangleBatchSpecification<PosType, TexType, ColType, ColSize> mSpecification;

			//viz::TriangleBatchRenderer<PosType, TexType, ColType, ColSize>* mRenderer;

			viz::TriangleBatchRenderParams mRenderParams;

			TriangleBatchRenderable* mRenderable;

			BeamVertexDataBuilder<PosType, TexType, ColType, ColSize> mVertexDataBuilder;

			int mRenderablePositionStride, mRenderableTexcoordStride;

			int mRenderUsage;

		public:

			BeamManager(std::string const& name, MeshCollisionManager* meshCollisionMgr, application::resourcesystem::ResourceManager* resourceMgr, int initialSize, Vector2 const& windowOrigin, Vector2 const& windowExtent)
				: mNumBeams(0)
				, mwMeshCollisionMgr(meshCollisionMgr)
				, mSpecification(false)
				, mRenderable(nullptr)
				, mRenderablePositionStride(0)
				, mRenderableTexcoordStride(0)
				, mRenderUsage(0)
			{
				mBeams.resize(initialSize);

				mRenderable = new TriangleBatchRenderable(64 * 3,
					mSpecification.getVertexMainStride(),
					mSpecification.getVertexTexcoordStride(),
					mSpecification.getColourOffset());

				mRenderParams.setColour(mpp::Colour::White);
				/*
				mRenderer = new viz::TriangleBatchRenderer<PosType, TexType, ColType, ColSize>(
					name, 
					&mSpecification, 
					"BeamProgram", 
					"BeamImage", 
					{ mRenderable }, 
					&mRenderParams, 
					windowOrigin, 
					windowExtent);
					*/
				// Get resource info
				auto resources = resourceMgr->getResourcesByType("Beam");
				for (auto const& res: resources)
				{
					BeamResource& br = static_cast<BeamResource&>(*res.get());

					string resName = br.getName();

					// Get uv-coords for section options, add to a definition.  We then
					// get the id for the definition based on a beam name we want to use,
					// and pass that id into addBeam.

					BeamType bt;
					bt.sectionOptions = br.getSectionOptions();
					bt.texCoordOptions = br.getTexCoordOptions();
					bt.headOffset = br.getHeadOffset();
					bt.bodyOffset = br.getBodyOffset();
					bt.baseOffset = br.getBaseOffset();

					auto image = static_cast<application::resourcesystem::ImageResource*>(br.getImage().get());
					bt.imageHeight = image->getHeight();

					// Add to lookup
					int index = (int)mBeamTypes.size();
					mBeamTypes.push_back(bt);
					mTypeNames[resName] = index;
				}
			}

			virtual ~BeamManager()
			{
				delete mRenderable;
				//delete mRenderer;
			}

			Beam& getBeam(int index)
			{
				return mBeams[index];
			}

			int getNumBeams() const
			{
				return mNumBeams;
			}

			int getTypeName(std::string const& name) const
			{
				return mTypeNames.at(name);
			}

			TriangleBatchRenderable* getRenderable()
			{
				return mRenderable;
			}

			viz::TriangleBatchRenderParams& getRenderParams()
			{
				return mRenderParams;
			}

			int getNumShards() const
			{
				return mNumShards;
			}

			int getCapacity() const
			{
				return (int)mBeams.size();
			}

			int getRenderUsage() const
			{
				return mRenderUsage;
			}

			int getRenderCapacity() const
			{
				return mRenderable->getVertexDataSize();
			}

			Beam& addBeam(int type, Vector2 const& position, float angle, float width, float length, ControlSetting accel, ControlSetting speed)
			{
				if (mNumBeams == (int)mBeams.size())
				{
					mBeams.resize(mNumBeams * 2);
				}

				auto& beam = mBeams[mNumBeams++];
				beam.initialise(type, position, length, width, angle, accel, speed);

				return beam;
			}

			void removeBeam(Beam& beam)
			{
				beam.setAlive(false);
			}

			void update(float frameTime, ObjectCollisionManager* objectMgr)
			{
				WP_UNUSED(frameTime);
				WP_UNUSED(objectMgr);

#ifdef WP_PROFILE_BUILD
//				startTimer();
#endif

				std::vector<firepower::ShardRenderData> shardRenderData;

				int numBeams = 0;
				int numVertices = 0;
				int numTriangles = 0;
				mNumShards = 0;

				for (int i = 0; i < mNumBeams; ++i)
				{
					auto& beam = mBeams[i];

					if (beam.update(mwMeshCollisionMgr, frameTime))
					{
						vector<Beam::Shard> const& shards = beam.getShards();
						int beamType = beam.getType();
						auto const& bt = mBeamTypes[beamType];

						Beam::TexCoordOptions texCoordOptions = bt.texCoordOptions;
						float widthOffset = shards.front().u;
						float widthScale = shards.back().u - widthOffset;

						firepower::ShardRenderData srd;

						srd.sectionOptions = bt.sectionOptions;
						srd.headOffset = bt.headOffset;
						srd.bodyOffset = bt.bodyOffset;
						srd.baseOffset = bt.baseOffset;
						srd.imageHeight = bt.imageHeight;

						for (uint32_t j = 0; j < shards.size() - 1; ++j)
						{
							numVertices += 6;
							numTriangles += 2;

							srd.nearVertices[0] = shards[j].nearVertex;
							srd.farVertices[0] = shards[j].farVertex;
							srd.nearVertices[1] = shards[j + 1].nearVertex;
							srd.farVertices[1] = shards[j + 1].farVertex;

							if (texCoordOptions == Beam::TexCoordOptions::Unitised)
							{
								srd.u[0] = shards[j].u;
								srd.v[0] = shards[j].v;
								srd.u[1] = shards[j + 1].u;
								srd.v[1] = shards[j + 1].v;
							}
							else
							{
								srd.u[0] = (shards[j].u - widthOffset) / widthScale;
								srd.v[0] = 1.0f;
								srd.u[1] = (shards[j + 1].u - widthOffset) / widthScale;
								srd.v[1] = 1.0f;
							}

							srd.beamIndex = numBeams;

							shardRenderData.push_back(srd);
							mNumShards++;
						}

						mBeams[numBeams++] = beam;
					}
				}

				mNumBeams = numBeams;
				mRenderUsage = 0;

				int vertexCount = 0, triangleCount = 0;
				if (mNumBeams > 0)
				{
					// Make sure buffers are sized correctly.
					size_t maxRequiredPosSize = numVertices * mSpecification.getVertexMainStride();
					size_t maxRequiredTexSize = numVertices * mSpecification.getVertexTexcoordStride();

					mRenderable->resizePositionData(maxRequiredPosSize);
					mRenderable->resizeTexcoordData(maxRequiredTexSize);

					mRenderUsage += mVertexDataBuilder(shardRenderData, mpp::Colour::White, &triangleCount, &vertexCount, mRenderable);
				}

				mRenderable->setVertexCount(vertexCount);
				mRenderable->setNumPrimitives(triangleCount);
				mRenderable->setBatchCount((int)shardRenderData.size());

#ifdef WP_PROFILE_BUILD
//				endTimer();
#endif
			}

			void render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, Vector2 const& windowOrigin, Vector2 const& windowExtent)
			{
				//mRenderer->render(renderSystem, resourceMgr, windowOrigin, windowExtent);
			}
		};

	} // firepower
} // WP_NAMESPACE