#pragma once

#include <vector>

#include <mpp/IndexedTriangleBatch.h>
#include <mpp/IndexedTriangleBatchSpecification.h>

#include "willpower/common/TriangleStripBatchRenderable.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Bomb.h"
#include "willpower/firepower/BombVertexDataBuilder.h"
#include "willpower/firepower/MeshCollisionManager.h"

#include "willpower/viz/IndexedTriangleBatchRenderer_old.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		
		template<typename PosType, typename TexType, typename ColType, int ColSize>
		class BombManager
		{
			std::vector<Bomb> mBombs;

			int mNumBombs, mNumArcs;

			MeshCollisionManager* mwMeshCollisionMgr;

			// Rendering
			mpp::IndexedTriangleBatchSpecification<PosType, TexType, ColType, ColSize> mSpecification;

			viz::IndexedTriangleBatchRenderer<PosType, TexType, ColType, ColSize>* mMeshRenderer;

			viz::IndexedTriangleBatchRenderParams mRenderParams;
			
			TriangleStripBatchRenderable* mRenderable;

			BombVertexDataBuilder<PosType, TexType, ColType, ColSize> mVertexDataBuilder;

			int mRenderablePositionStride, mRenderableTexcoordStride;
				
			int mRenderableIndexWidth;

			int mRenderUsage;

		public:

			BombManager(std::string const& name, MeshCollisionManager* meshCollisionMgr, int initialSize, Vector2 const& windowOrigin, Vector2 const& windowExtent)
				: mNumBombs(0)
				, mNumArcs(0)
				, mwMeshCollisionMgr(meshCollisionMgr)
				, mSpecification(false, 32)
				, mRenderable(nullptr)
				, mRenderablePositionStride(0)
				, mRenderableTexcoordStride(0)
				, mRenderableIndexWidth(0)
				, mRenderUsage(0)
			{
				mBombs.resize(initialSize);

				mRenderable = new TriangleStripBatchRenderable(128,
					mSpecification.getIndexWidth(),
					mSpecification.getVertexMainStride(),
					mSpecification.getVertexTexcoordStride(),
					mSpecification.getColourOffset());

				mRenderParams.setColour(mpp::Colour::Cyan);

				mMeshRenderer = new viz::IndexedTriangleBatchRenderer<PosType, TexType, ColType, ColSize>(
					name, 
					&mSpecification, 
					"Bomb",
					{ mRenderable }, 
					&mRenderParams, 
					windowOrigin,
					windowExtent);
			}

			virtual ~BombManager()
			{
				delete mRenderable;
				delete mMeshRenderer;
			}

			Bomb& addBomb(int type, 
				         Vector2 const& position, float radius, float depth, bool clipArcs, 
				         Bomb::LifeOptions const& lifeOptions, 
			             ControlSetting accelValue,
				         ControlSetting speedValue,
				         ControlSetting rotateValue,
				         std::vector<Bomb::Arc> const& arcs)
			{
				WP_UNUSED(type);

				if (mNumBombs == (int)mBombs.size())
				{
					mBombs.resize(mNumBombs * 2);
				}

				auto& bomb = mBombs[mNumBombs++];
				bomb.initialise(position, radius, depth, clipArcs, 
					lifeOptions,
					accelValue, 
					speedValue, 
					rotateValue, 
					arcs);

				return bomb;
			}

			Bomb& getBomb(int index)
			{
				return mBombs[index];
			}

			Bomb const& getBomb(int index) const
			{
				return mBombs[index];
			}

			IndexedTriangleBatchRenderable* getRenderable()
			{
				return mRenderable;
			}

			viz::IndexedTriangleBatchRenderParams& getRenderParams()
			{
				return mRenderParams;
			}

			int getNumBombs() const
			{
				return mNumBombs;
			}

			int getNumArcs() const
			{
				return mNumArcs;
			}

			int getCapacity() const
			{
				return (int)mBombs.size();
			}

			int getRenderUsage() const
			{
				return mRenderUsage;
			}

			int getRenderCapacity() const
			{
				return mRenderable->getVertexDataSize();
			}

			void update(float frameTime, ObjectCollisionManager* objectMgr)
			{
				WP_UNUSED(frameTime);
				WP_UNUSED(objectMgr);

#ifdef WP_PROFILE_BUILD
//				startTimer();
#endif

				const float scale = 1.0f;

				// Intersect arcs and calculate space required.
				std::vector<firepower::ArcRenderData> arcRenderData;

				int numBombs = 0;
				int numVertices = 0;
				int numTriangles = 0;
				mNumArcs = 0;

				for (int i = 0; i < mNumBombs; ++i)
				{
					auto& bomb = mBombs[i];

					if (bomb.update(mwMeshCollisionMgr, frameTime))
					{
						vector<Bomb::Arc> const& arcs = bomb.getArcs();

						for (auto const& arc: arcs)
						{
							// Store to be rendered later
							float angleDiff = arc.second > arc.first ? arc.second - arc.first : (360.0f - arc.first) + arc.second;
							int numArcSegs = (int)((mBombs[i].getRadius() * angleDiff * scale) / 360.0f);

							int numSegs = max(16, numArcSegs);
							numTriangles += numSegs * 2;
							numVertices += 2;

							firepower::ArcRenderData ard;
							ard.angle = arc.first;
							ard.size = angleDiff;
							ard.numSegments = numSegs;
							ard.bombIndex = numBombs;

							arcRenderData.push_back(ard);
							mNumArcs++;
						}

						mBombs[numBombs++] = bomb;
					}
				}

				numVertices += numTriangles;

				mNumBombs = numBombs;
				mRenderUsage = 0;

				int vertexCount = 0, triangleCount = 0;
				if (mNumBombs > 0)
				{
					// Make sure buffers are sized correctly.
					size_t maxRequiredPosSize = numVertices * mSpecification.getVertexMainStride();
					size_t maxRequiredTexSize = numVertices * mSpecification.getVertexTexcoordStride();
					size_t maxRequiredIndexSize = numTriangles * 3 * (mSpecification.getIndexWidth() / 8);

					mRenderable->resizePositionData(maxRequiredPosSize);
					mRenderable->resizeTexcoordData(maxRequiredTexSize);
					mRenderable->resizeIndexData(maxRequiredIndexSize);

					mRenderUsage += mVertexDataBuilder(arcRenderData, mBombs, scale, mpp::Colour::White, &triangleCount, &vertexCount, mRenderable);
				}

				mRenderable->setVertexCount(vertexCount);
				mRenderable->setNumPrimitives(triangleCount);
				mRenderable->setBatchCount((int)arcRenderData.size());

#ifdef WP_PROFILE_BUILD
//				endTimer();
#endif
			}

			void render(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr, Vector2 const& windowOrigin, Vector2 const& windowExtent)
			{
				mMeshRenderer->render(renderSystem, resourceMgr, windowOrigin, windowExtent);
			}
		};

	} // firepower
} // WP_NAMESPACE