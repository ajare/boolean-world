#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace bw::app {

// Runs a periodic job on a dedicated thread against the latest complete,
// immutable snapshot. A publication never changes the snapshot held by an
// in-flight tick; that tick's local shared_ptr retires only after it returns.
class PeriodicSnapshotWorker {
public:
  using SnapshotPtr = std::shared_ptr<void const>;
  using Tick = std::function<void(SnapshotPtr const&)>;

  PeriodicSnapshotWorker(std::chrono::steady_clock::duration interval,
                         Tick tick);
  ~PeriodicSnapshotWorker();

  PeriodicSnapshotWorker(PeriodicSnapshotWorker const&) = delete;
  PeriodicSnapshotWorker& operator=(PeriodicSnapshotWorker const&) = delete;

  void publish(SnapshotPtr snapshot);

private:
  void run(std::stop_token stopToken);

  std::chrono::steady_clock::duration mInterval;
  Tick mTick;
  std::atomic<SnapshotPtr> mLatestSnapshot;
  std::mutex mWaitMutex;
  std::condition_variable mWake;
  std::jthread mThread;
};

}  // namespace bw::app
