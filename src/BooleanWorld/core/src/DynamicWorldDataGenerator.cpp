#include <algorithm>
#include <chrono>

#include <willpower/common/MathsUtils.h>

#include "core/DynamicWorldDataGenerator.h"
#include "core/World.h"
#include "core/ArrangementWorldDataGenerator.h"
#include "core/Defines.h"

namespace bw {
namespace core {
using namespace std;

DynamicWorldDataGenerator::DynamicWorldDataGenerator(World const* world)
    : WorldDataGenerator(), mClippingIdGenerator(0), mWorld(world), mAlwaysUpdateVertices(false), mAllowCommitIfVisible(false), mNumGenerationsInProgress(0), mNumGenerationsComplete(0), mNumCommits(0), mLastGenTime(0), mScheduledGenerationRunning(false), mScheduledGenerationRequested(false), mScheduledGenerationInterval(5.0f) {
  ArrangementWorldDataGenerator generator;
  mActiveClipping.worldData = make_shared<ArrangementWorldData>(
      generator.getWorldData(),
      world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      world->getStepThreshold());
}

DynamicWorldDataGenerator::~DynamicWorldDataGenerator() {
  stopGenerationSchedule();
}

DynamicWorldDataGenerator::DynamicWorldDataGenerator(DynamicWorldDataGenerator const& other)
    : mClippingIdGenerator(0), mWorld(nullptr), mAlwaysUpdateVertices(false), mAllowCommitIfVisible(false), mNumGenerationsInProgress(0), mNumGenerationsComplete(0), mNumCommits(0), mLastGenTime(0), mScheduledGenerationRunning(false), mScheduledGenerationRequested(false), mScheduledGenerationInterval(5.0f) {
  copyFrom(other);
}

DynamicWorldDataGenerator& DynamicWorldDataGenerator::operator=(DynamicWorldDataGenerator const& other) {
  if (this != &other) {
    copyFrom(other);
  }
  return *this;
}

void DynamicWorldDataGenerator::copyFrom(DynamicWorldDataGenerator const& other) {
  scoped_lock lock(mGenMutex, other.mGenMutex);
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
  mScheduledGenerationInterval.store(other.mScheduledGenerationInterval.load());
}

WorldDataGenerator* DynamicWorldDataGenerator::copy() {
  return new DynamicWorldDataGenerator(*this);
}

void DynamicWorldDataGenerator::setAllowCommitIfVisible(bool allow) {
  mAllowCommitIfVisible = allow;
}

bool DynamicWorldDataGenerator::getAllowCommitIfVisible() const {
  return mAllowCommitIfVisible;
}

void DynamicWorldDataGenerator::setAlwaysUpdateVertices(bool update) {
  mAlwaysUpdateVertices = update;
}

bool DynamicWorldDataGenerator::getAlwaysUpdateVertices() const {
  return mAlwaysUpdateVertices;
}

uint32_t DynamicWorldDataGenerator::getNumGenerationsInProgress() const {
  return mNumGenerationsInProgress;
}

uint32_t DynamicWorldDataGenerator::getNumGenerationsComplete() const {
  return mNumGenerationsComplete;
}

uint32_t DynamicWorldDataGenerator::getNumCommits() const {
  return mNumCommits;
}

uint64_t DynamicWorldDataGenerator::getLastGenTime() const {
  return mLastGenTime;
}

vector<Primitive*> DynamicWorldDataGenerator::getSourceClippingPrimitives() const {
  lock_guard<mutex> lock(mGenMutex);

  return mNextClipping.primitives;
}

vector<Primitive*> DynamicWorldDataGenerator::getActiveClippingPrimitives() const {
  lock_guard<mutex> lock(mGenMutex);

  return mActiveClipping.primitives;
}

void DynamicWorldDataGenerator::setScheduledGenerationInterval(float interval) {
  mScheduledGenerationInterval = interval;
}

float DynamicWorldDataGenerator::getScheduledGenerationInterval() const {
  return mScheduledGenerationInterval;
}

bool DynamicWorldDataGenerator::isScheduledGenerationRunning() const {
  return mScheduledGenerationRunning;
}

DynamicWorldDataGenerator::GenerationCallbackToken
DynamicWorldDataGenerator::registerGenerationCallback(
    GenerationCompleteCallback callback) {
  lock_guard<mutex> lock(mGenMutex);

  auto registration = make_shared<GenerationCallbackRegistration>();
  registration->token = mNextGenerationCallbackToken++;
  registration->callback = move(callback);
  mCallbacks.push_back(registration);
  return registration->token;
}

void DynamicWorldDataGenerator::unregisterGenerationCallback(
    GenerationCallbackToken token) {
  if (token == InvalidGenerationCallbackToken) {
    return;
  }

  shared_ptr<GenerationCallbackRegistration> registration;
  {
    lock_guard<mutex> lock(mGenMutex);
    auto found = find_if(
        mCallbacks.begin(), mCallbacks.end(),
        [token](auto const& callback) { return callback->token == token; });
    if (found == mCallbacks.end()) {
      return;
    }

    registration = *found;
  }

  {
    unique_lock<mutex> lock(registration->mutex);
    registration->registered = false;
    registration->noCallbacksInProgress.wait(
        lock,
        [&registration] { return registration->callbacksInProgress == 0; });
  }

  lock_guard<mutex> lock(mGenMutex);
  erase_if(
      mCallbacks,
      [&registration](auto const& callback) { return callback == registration; });
}

std::vector<Primitive*> DynamicWorldDataGenerator::preparePrimitives(vector<Primitive*>& primitives, PrimitiveProcessingStats* stats) const {
  vector<Primitive*> updatedPrimitives;

  updatedPrimitives.reserve(primitives.size());

  // Generate vertices for non-visible Primitives
  for (auto primitive : primitives) {
    wp::Vector2 boundsMin, boundsMax;
    primitive->getBounds().getExtents(boundsMin, boundsMax);
    auto visible = wp::MathsUtils::boxIntersectsTriangle(
        boundsMin,
        boundsMax,
        mViewTriangle[0],
        mViewTriangle[1],
        mViewTriangle[2]);

    if (visible) {
      stats->visibleCount++;
    }

    if (!primitive->isStatic() && (mAlwaysUpdateVertices || !visible)) {
      primitive->updateVertexPositions();
      updatedPrimitives.push_back(primitive);

      stats->updateVertexCount++;
    }
  }

  return updatedPrimitives;
}

DynamicWorldDataGenerator::GenerationInput
DynamicWorldDataGenerator::snapshotGenerationInput(
    World const* world, bool regetPrimitives) {
  lock_guard<mutex> lock(mGenMutex);

  if (regetPrimitives) {
    mNextClipping.primitives = selectAndOrderPrimitives(
        *world, getLayerSelection());
    mNextClipping.layerSelection = getLayerSelection();
  }

  auto primitives = mNextClipping.primitives;
  auto primStats = mNextClipping.stats.prim;
  primStats.candidateCount = uint32_t(primitives.size());
  primStats.visibleCount = 0;
  primStats.updateVertexCount = 0;

  auto updatedPrimitives = preparePrimitives(primitives, &primStats);
  auto arrangementPrimitives = SnapshotPrimitives(primitives);

  return {move(arrangementPrimitives),
          move(primitives),
          move(updatedPrimitives),
          mNextClipping.layerSelection,
          primStats,
          world->getExtents(),
          float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
          world->getStepThreshold()};
}

void DynamicWorldDataGenerator::generateWorldData(GenerationInput input) {
  auto clippingId = mClippingIdGenerator++;
  GenerationDetails details{
      clippingId,
      GenerationState::Generating,
      0,
      {input.primStats, {}}};

  fireCallbacks(details);

  // Everything below runs on a worker for asynchronous generations. Its
  // geometry and world settings were copied before the worker was posted.
  mNumGenerationsInProgress++;
  wp::Timer timer;

  Stats stats{input.primStats, {}};
  auto results = make_shared<ArrangementWorldData>(
      arr::BuildArrangement(input.primitives, &stats.arrangement),
      input.worldExtents,
      input.gridCellSize,
      input.stepThreshold,
      &stats.arrangement);

  mLastGenTime = timer.elapsedNanoseconds();

  mPendingClippings.push_bounded(
      {clippingId,
       move(results),
       move(input.sourcePrimitives),
       move(input.updatedPrimitives),
       input.layerSelection,
       stats,
       mLastGenTime},
      MaxPendingGenerations);

  mNumGenerationsInProgress--;
  mNumGenerationsComplete++;

  details.state = GenerationState::Generated;
  details.genTimeNs = mLastGenTime;
  details.stats = stats;

  fireCallbacks(details);
}

void DynamicWorldDataGenerator::fireCallbacks(GenerationDetails const& details) {
  vector<shared_ptr<GenerationCallbackRegistration>> callbacks;
  {
    lock_guard<mutex> lock(mGenMutex);
    callbacks = mCallbacks;
  }

  for (auto const& registration : callbacks) {
    {
      unique_lock<mutex> lock(registration->mutex);
      if (!registration->registered) {
        continue;
      }
      registration->callbacksInProgress++;
    }

    try {
      registration->callback(details);
    } catch (...) {
      lock_guard<mutex> lock(registration->mutex);
      registration->callbacksInProgress--;
      registration->noCallbacksInProgress.notify_all();
      throw;
    }

    lock_guard<mutex> lock(registration->mutex);
    registration->callbacksInProgress--;
    registration->noCallbacksInProgress.notify_all();
  }
}

bool DynamicWorldDataGenerator::canCommit(Clipping const& clipping) {
  auto const& requestedSelection = getLayerSelection();
  if (clipping.layerSelection != requestedSelection) {
    return false;
  }
  if (mAllowCommitIfVisible ||
      clipping.layerSelection != mActiveClipping.layerSelection) {
    return true;
  }

  // Only commit if none of the updated vertices are visible
  for (auto primitive : clipping.updatedPrimitives) {
    wp::Vector2 boundsMin, boundsMax;
    primitive->getBounds().getExtents(boundsMin, boundsMax);

    if (wp::MathsUtils::boxIntersectsTriangle(
            boundsMin,
            boundsMax,
            mViewTriangle[0],
            mViewTriangle[1],
            mViewTriangle[2])) {
      return false;
    }
  }

  return true;
}

void DynamicWorldDataGenerator::checkCommitPendingClipping() {
  Clipping clipping;
  auto const stale = [this](Clipping const& candidate) {
    return candidate.layerSelection != getLayerSelection();
  };
  while (mPendingClippings.try_pop_if(clipping, stale)) {
  }

  auto const checker = [this](Clipping const& candidate) {
    return canCommit(candidate);
  };
  if (!mPendingClippings.try_pop_if(clipping, checker)) {
    return;
  }

  auto const clippingId = clipping.id;
  Stats stats;
  {
    lock_guard<mutex> lock(mGenMutex);
    mActiveClipping = move(clipping);
    stats = mActiveClipping.stats;
    mNumCommits++;
  }

  fireCallbacks({clippingId, GenerationState::Committed, 0, stats});
}

WorldDataPtr DynamicWorldDataGenerator::getWorldData(World const* world) {
  {
    lock_guard<mutex> lock(mGenMutex);
    mNextClipping.primitives = selectAndOrderPrimitives(
        *world, getLayerSelection());
    mNextClipping.layerSelection = getLayerSelection();
    mNextClipping.stats.prim = {
        uint32_t(mNextClipping.primitives.size()),
        0,
        0};
  }

  if (mNumGenerationsComplete == 0) {
    generateWorldData(snapshotGenerationInput(world, false));
  }

  checkCommitPendingClipping();
  return mActiveClipping.worldData;
}

void DynamicWorldDataGenerator::generate(World const* world, bool regetPrimitives) {
  auto input = snapshotGenerationInput(world, regetPrimitives);
  mExecutorRuntime.thread_pool_executor()->post(
      [this, input = move(input)]() mutable {
        generateWorldData(move(input));
      });
}

void DynamicWorldDataGenerator::generate(bool regetPrimitives) {
  if (mWorld) {
    generate(mWorld, regetPrimitives);
  }
}

void DynamicWorldDataGenerator::generateBlocking() {
  if (mWorld) {
    generateWorldData(snapshotGenerationInput(mWorld, true));
  }
}

void DynamicWorldDataGenerator::handleEvents(uint32_t events) {
  auto scheduled = mScheduledGenerationRequested.exchange(false);
  if (scheduled || events & BW_PRIMITIVE_GLOBAL_EVENT_CLIP) {
    generate();
  }
}

void DynamicWorldDataGenerator::handleLayerSelectionChanged() {
  if (mWorld && mNumGenerationsComplete > 0) {
    generate(true);
  }
}

void DynamicWorldDataGenerator::generateOnInterval() {
  while (true) {
    // Sleep in multiple phases so we can check for termination more regularly.
    // The scheduler only requests work; the next main-thread update captures
    // the live primitive snapshot before posting the generation worker.
    auto sleepAmt = 0.0f;
    auto sleepTime = getScheduledGenerationInterval();
    auto sleepIters = int(ceil(sleepTime));

    for (int i = 0; i < sleepIters; ++i) {
      auto sleepSeconds = min(sleepTime - sleepAmt, 1.0f);

      this_thread::sleep_for(chrono::duration<float>(sleepSeconds));
      sleepAmt += sleepSeconds;

      if (!mScheduledGenerationRunning) {
        return;
      }
    }

    mScheduledGenerationRequested = true;
  }
}

void DynamicWorldDataGenerator::startGenerationSchedule(float interval) {
  setScheduledGenerationInterval(interval);

  if (!mScheduledGenerationRunning) {
    mScheduledGenerationRunning = true;
    mScheduledGenerationRequested = false;

    // Preserve the immediate first generation while taking its snapshot on
    // the caller (main) thread.
    generate();
    mScheduledWorker = mExecutorRuntime.thread_pool_executor()->submit([this] {
      generateOnInterval();
    });
  }
}

void DynamicWorldDataGenerator::stopGenerationSchedule() {
  if (mScheduledGenerationRunning) {
    mScheduledGenerationRunning = false;
    mScheduledWorker.get();
    mScheduledGenerationRequested = false;
  }
}

}  // namespace core
}  // namespace bw
