#include <chrono>

#include <willpower/common/MathsUtils.h>

#include "core/DynamicWorldDataGenerator.h"
#include "core/World.h"
#include "core/Clipper.h"
#include "core/ClipperUtils.h"

#define MAX_NUM_PENDING_CLIPPINGS			4

namespace bw
{
	namespace core
	{
		using namespace std;

		DynamicWorldDataGenerator::DynamicWorldDataGenerator(World const* world)
			: WorldDataGenerator()
			, mClippingIdGenerator(0)
			, mWorld(world)
			, mAlwaysUpdateVertices(false)
			, mAllowCommitIfVisible(false)
			, mNumGenerationsInProgress(0)
			, mNumGenerationsComplete(0)
			, mNumCommits(0)
			, mLastGenTime(0)
			, mScheduledGenerationRunning(false)
			, mScheduledGenerationInterval(5.0f)
		{
			mActiveClipping.worldData = {
				world->getExtents(),
				(float)(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
				{},
				{},
				{},
				{},
				{},
				{},
				world->getFrameNumber()
			};
		}

		DynamicWorldDataGenerator::~DynamicWorldDataGenerator()
		{
			stopGenerationSchedule();
		}

		DynamicWorldDataGenerator::DynamicWorldDataGenerator(DynamicWorldDataGenerator const& other)
			: mWorld(nullptr)
			, mClippingIdGenerator(0)
			, mAlwaysUpdateVertices(false)
			, mAllowCommitIfVisible(false)
			, mNumGenerationsInProgress(0)
			, mNumGenerationsComplete(0)
			, mNumCommits(0)
			, mLastGenTime(0)
		{
			WorldDataGenerator::copyFrom(other);
		}

		DynamicWorldDataGenerator& DynamicWorldDataGenerator::operator=(DynamicWorldDataGenerator const& other)
		{
			WorldDataGenerator::copyFrom(other);
			return *this;
		}

		void DynamicWorldDataGenerator::copyFrom(DynamicWorldDataGenerator const& other)
		{
			WorldDataGenerator::copyFrom(other);

			mWorld = other.mWorld;
			mClippingIdGenerator.store(other.mClippingIdGenerator.load());
			mAlwaysUpdateVertices = other.mAlwaysUpdateVertices;
			mAllowCommitIfVisible = other.mAllowCommitIfVisible;
			mActiveClipping = other.mActiveClipping;
			mNextClipping = other.mNextClipping;
			mNumGenerationsInProgress = 0;
			mNumGenerationsComplete.store(other.mNumGenerationsComplete.load());
			mNumCommits.store(other.mNumCommits.load());
			mLastGenTime.store(other.mLastGenTime.load());
		}

		WorldDataGenerator* DynamicWorldDataGenerator::copy()
		{
			return new DynamicWorldDataGenerator(*this);
		}

		void DynamicWorldDataGenerator::setAllowCommitIfVisible(bool allow)
		{
			mAllowCommitIfVisible = allow;
		}

		bool DynamicWorldDataGenerator::getAllowCommitIfVisible() const
		{
			return mAllowCommitIfVisible;
		}

		void DynamicWorldDataGenerator::setAlwaysUpdateVertices(bool update)
		{
			mAlwaysUpdateVertices = update;
		}

		bool DynamicWorldDataGenerator::getAlwaysUpdateVertices() const
		{
			return mAlwaysUpdateVertices;
		}

		uint32_t DynamicWorldDataGenerator::getNumGenerationsInProgress() const
		{
			return mNumGenerationsInProgress;
		}
		
		uint32_t DynamicWorldDataGenerator::getNumGenerationsComplete() const
		{
			return mNumGenerationsComplete;
		}

		uint32_t DynamicWorldDataGenerator::getNumCommits() const
		{
			return mNumCommits;
		}

		uint64_t DynamicWorldDataGenerator::getLastGenTime() const
		{
			return mLastGenTime;
		}

		vector<Primitive*> DynamicWorldDataGenerator::getSourceClippingPrimitives() const
		{
			lock_guard<mutex> lock(mGenMutex);

			return mNextClipping.primitives;
		}

		void DynamicWorldDataGenerator::setScheduledGenerationInterval(float interval)
		{
			mScheduledGenerationInterval = interval;
		}

		float DynamicWorldDataGenerator::getScheduledGenerationInterval() const
		{
			return mScheduledGenerationInterval;
		}

		bool DynamicWorldDataGenerator::isScheduledGenerationRunning() const
		{
			return mScheduledGenerationRunning;
		}

		void DynamicWorldDataGenerator::registerGenerationCallback(GenerationCompleteCallback callback)
		{
			lock_guard<mutex> lock(mGenMutex);
			mCallbacks.push_back(callback);
		}

		std::vector<Primitive*> DynamicWorldDataGenerator::preparePrimitives(vector<Primitive*>& primitives, PrimitiveProcessingStats* stats) const
		{
			vector<Primitive*> updatedPrimitives;

			updatedPrimitives.reserve(primitives.size());

			sort(primitives.begin(), primitives.end(), SortPrimitivesByPriority());

			auto viewVertices = getViewVertices();

			// Generate vertices for non-visible Primitives
			for (auto primitive : primitives)
			{
				auto visible = primitiveInView(primitive, viewVertices);

				if (visible)
				{
					stats->visibleCount++;
				}

				if (!primitive->isStatic() && (mAlwaysUpdateVertices || !visible))
				{
					primitive->updateVertexPositions();
					updatedPrimitives.push_back(primitive);

					stats->updateVertexCount++;
				}
			}

			return updatedPrimitives;
		}

		void DynamicWorldDataGenerator::generateWorldData(World const* world)
		{
			// Create details to send back to clientS
			auto clippingId = mClippingIdGenerator++;

			vector<Primitive*> prims;
			PrimitiveProcessingStats primStats;

			if (true)
			{
				lock_guard<mutex> lock(mGenMutex);
				prims = mNextClipping.primitives;
				primStats = mNextClipping.primStats;

				primStats.candidateCount = (uint32_t)prims.size();
			}

			GenerationDetails details{
				clippingId,
				GenerationState::Generating,
				0,
				{ primStats, {}, {} }
			};

			fireCallbacks(details);

			// Generation
			mNumGenerationsInProgress++;
			wp::Timer timer;

			auto updatedPrimitives = preparePrimitives(prims, &primStats);
			auto clipResults = clipPrimitives(prims, world, true);

			// Set up data to return
			WorldData results = {
				world->getExtents(),
				(float)(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
				ClipperUtils::convertClipper2PolygonsToClippedPolygons(clipResults.borderPolygons, nullptr),
				ClipperUtils::convertClipper2PolygonsToClippedPolygons(clipResults.arrangementPolygons, nullptr),
				clipResults.borderVertexData,
				clipResults.graph,
				clipResults.stats,
				primStats,
				world->getFrameNumber(),
			};

			mLastGenTime = timer.elapsedNanoseconds();
			details.stats = results.getStats();
			
			mPendingClippings.push({
				clippingId,
				move(results),
				move(prims),
				move(updatedPrimitives),
				primStats,
				mLastGenTime
			});

			mNumGenerationsInProgress--;
			mNumGenerationsComplete++;

			// Update details for client
			details.state = GenerationState::Generated;
			details.genTimeNs = mLastGenTime;

			fireCallbacks(details);
		}

		void DynamicWorldDataGenerator::fireCallbacks(GenerationDetails const& details)
		{
			for (auto callback : mCallbacks)
			{
				callback(details);
			}
		}

		bool DynamicWorldDataGenerator::canCommit(Clipping const& clipping)
		{
			if (mAllowCommitIfVisible)
			{
				return true;
			}

			// Only commit if none of the updated vertices are visible
			auto viewVertices = getViewVertices();

			for (auto primitive : clipping.updatedPrimitives)
			{
				auto const& bounds = primitive->getBounds();

				wp::Vector2 boundsMin, boundsMax;

				bounds.getExtents(boundsMin, boundsMax);

				if (wp::MathsUtils::boxIntersectsTriangle(boundsMin, boundsMax, viewVertices[0], viewVertices[1], viewVertices[2]))
				{
					return false;
				}
			}

			return true;
		}

		void DynamicWorldDataGenerator::checkCommitPendingClipping()
		{
			auto checker = bind(&DynamicWorldDataGenerator::canCommit, this, std::placeholders::_1);

			if (mPendingClippings.can_pop(checker))
			{
				auto clipping = mPendingClippings.pop();

				if (clipping.has_value())
				{
					lock_guard<mutex> lock(mGenMutex);

					mActiveClipping = move(clipping.value());
					mNumCommits++;

					GenerationDetails details{
						clipping->id,
						GenerationState::Committed,
						0,
						{}
					};

					fireCallbacks(details);
				}
			}
		}

		WorldData DynamicWorldDataGenerator::getWorldData(World const* world)
		{
			auto primitives = getPrimitives(world, getActiveLayer());

			if (mNumGenerationsComplete == 0)
			{
				mNextClipping.primitives = primitives;

				mNextClipping.primStats = {
					(uint32_t)primitives.size(),
					0,
					0
				};

				generateWorldData(world);
			}
			else
			{
				lock_guard<mutex> lock(mGenMutex);

				mNextClipping.primitives = primitives;

				mNextClipping.primStats = {
					(uint32_t)primitives.size(),
					0,
					0
				};
			}

			checkCommitPendingClipping();

			return mActiveClipping.worldData;
		}

		void DynamicWorldDataGenerator::generate(World const* world, NarrowPhaseCulling culling, bool regetPrimitives)
		{
			if (culling != getNarrowPhaseCulling())
			{
				setNarrowPhaseCulling(culling);
				regetPrimitives = true;
			}

			if (regetPrimitives)
			{
				// Recalculate visible Primitives
				lock_guard<mutex> lock(mGenMutex);

				mNextClipping.primitives = getPrimitives(world, getActiveLayer());
			}

			mExecutorRuntime.thread_pool_executor()->post([this, world] {
				generateWorldData(world);
			});
		}

		void DynamicWorldDataGenerator::generate(bool regetPrimitives)
		{
			if (regetPrimitives)
			{
				// Recalculate visible Primitives
				lock_guard<mutex> lock(mGenMutex);

				mNextClipping.primitives = getPrimitives(mWorld, getActiveLayer());
			}
			
			mExecutorRuntime.thread_pool_executor()->post([this] {
				generateWorldData(mWorld);
			});
		}

		void DynamicWorldDataGenerator::generateBlocking()
		{
			mNextClipping.primitives = getPrimitives(mWorld, getActiveLayer());
			generateWorldData(mWorld);
		}

		void DynamicWorldDataGenerator::handleEvents(uint32_t events)
		{
			if (events & BW_PRIMITIVE_GLOBAL_EVENT_CLIP)
			{
				generate();
			}
		}

		void DynamicWorldDataGenerator::generateOnInterval()
		{
			while (true)
			{
				generateWorldData(mWorld);

				// Sleep in multiple phases so we can check for termination more regularly
				auto sleepAmt = 0.0f;
				auto sleepTime = getScheduledGenerationInterval();
				auto sleepIters = int(ceil(sleepTime));

				for (int i = 0; i < sleepIters; ++i)
				{
					auto sleepSeconds = min(sleepTime - sleepAmt, 1.0f);

					this_thread::sleep_for(chrono::duration<float>(sleepSeconds));
					sleepAmt += sleepSeconds;

					if (!mScheduledGenerationRunning)
					{
						return;
					}
				}
			}
		}

		void DynamicWorldDataGenerator::startGenerationSchedule(float interval)
		{
			setScheduledGenerationInterval(interval);

			if (!mScheduledGenerationRunning)
			{
				mScheduledGenerationRunning = true;

				mScheduledWorker = mExecutorRuntime.thread_pool_executor()->submit([this] {
					generateOnInterval();
				});
			}
		}

		void DynamicWorldDataGenerator::stopGenerationSchedule()
		{
			if (mScheduledGenerationRunning)
			{
				mScheduledGenerationRunning = false;
				mScheduledWorker.get();
			}
		}

	} // bw
} // core
