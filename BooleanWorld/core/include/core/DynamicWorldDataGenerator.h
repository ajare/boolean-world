#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

// Disable padding warnings for concurrencpp
#pragma warning(push)
#pragma warning(disable : 4324)
#include <concurrencpp/concurrencpp.h>
#pragma warning(pop)

#include "core/WorldDataGenerator.h"
#include "core/Stats.h"
#include "core/ThreadSafeQueue.h"

namespace bw {
namespace core {
class DynamicWorldDataGenerator : public WorldDataGenerator {
public:
  struct Clipping {
    uint32_t id;
    WorldDataPtr worldData;
    std::vector<Primitive*> primitives;
    std::vector<Primitive*> updatedPrimitives;
    LayerSelection layerSelection;
    PrimitiveProcessingStats primStats;
    uint64_t genTimeNs{0};
  };

  enum struct GenerationState {
    Generating,
    Generated,
    Committed
  };

  struct GenerationDetails {
    uint32_t clippingId;
    GenerationState state;
    uint64_t genTimeNs;
    Stats stats;
  };

  typedef std::function<void(GenerationDetails const& details)> GenerationCompleteCallback;

private:
  std::atomic_uint32_t mClippingIdGenerator;

  World const* mWorld;

  Clipping mActiveClipping, mNextClipping;

  ThreadSafeQueue<Clipping> mPendingClippings;

  std::vector<GenerationCompleteCallback> mCallbacks;

  mutable std::mutex mGenMutex;

  concurrencpp::runtime mExecutorRuntime;

  bool mAlwaysUpdateVertices, mAllowCommitIfVisible;

  std::atomic_uint32_t mNumGenerationsInProgress;

  std::atomic_uint32_t mNumGenerationsComplete, mNumCommits;

  std::atomic_uint64_t mLastGenTime;

  // Regularly-scheduled worker
  concurrencpp::result<void> mScheduledWorker;

  std::atomic_bool mScheduledGenerationRunning;

  std::atomic<float> mScheduledGenerationInterval;

private:
  void copyFrom(DynamicWorldDataGenerator const& other);

  void generateWorldData(World const* world);

  std::vector<Primitive*> preparePrimitives(std::vector<Primitive*>& primitives, PrimitiveProcessingStats* stats) const;

  void handleEvents(uint32_t events) override;

  void handleLayerSelectionChanged() override;

  void checkCommitPendingClipping();

  void generateOnInterval();

  bool canCommit(Clipping const& clipping);

  void fireCallbacks(GenerationDetails const& details);

public:
  explicit DynamicWorldDataGenerator(World const* world);

  ~DynamicWorldDataGenerator();

  DynamicWorldDataGenerator(DynamicWorldDataGenerator const& other);

  DynamicWorldDataGenerator& operator=(DynamicWorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() override;

  void setAlwaysUpdateVertices(bool update);

  bool getAlwaysUpdateVertices() const;

  void setAllowCommitIfVisible(bool allow);

  bool getAllowCommitIfVisible() const;

  uint32_t getNumGenerationsInProgress() const;

  uint32_t getNumGenerationsComplete() const;

  uint32_t getNumCommits() const;

  uint64_t getLastGenTime() const;

  std::vector<Primitive*> getSourceClippingPrimitives() const;

  std::vector<Primitive*> getActiveClippingPrimitives() const;

  void setScheduledGenerationInterval(float interval);

  float getScheduledGenerationInterval() const;

  bool isScheduledGenerationRunning() const;

  void registerGenerationCallback(GenerationCompleteCallback callback);

  WorldDataPtr getWorldData(World const* world) override;

  void generate(World const* world, bool regetPrimitives = false) override;

  void generate(bool regetPrimitives = false);

  void generateBlocking();

  void startGenerationSchedule(float interval);

  void stopGenerationSchedule();
};

}  // namespace core
}  // namespace bw
