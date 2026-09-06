#include "PeriodicSnapshotWorker.h"

#include <stdexcept>
#include <utility>

namespace bw::app {

PeriodicSnapshotWorker::PeriodicSnapshotWorker(
    std::chrono::steady_clock::duration interval, Tick tick)
    : mInterval(interval), mTick(std::move(tick)), mLatestSnapshot() {
  if (interval <= std::chrono::steady_clock::duration::zero()) {
    throw std::invalid_argument("Snapshot worker interval must be positive");
  }
  if (!mTick) {
    throw std::invalid_argument("Snapshot worker requires a tick callback");
  }
  mThread = std::jthread(
      [this](std::stop_token stopToken) { run(stopToken); });
}

PeriodicSnapshotWorker::~PeriodicSnapshotWorker() {
  mThread.request_stop();
  mWake.notify_all();
  if (mThread.joinable()) {
    mThread.join();
  }
  mLatestSnapshot.store({}, std::memory_order_release);
}

void PeriodicSnapshotWorker::publish(SnapshotPtr snapshot) {
  mLatestSnapshot.store(std::move(snapshot), std::memory_order_release);
}

void PeriodicSnapshotWorker::setInterval(
    std::chrono::steady_clock::duration interval) {
  if (interval <= std::chrono::steady_clock::duration::zero()) {
    throw std::invalid_argument("Snapshot worker interval must be positive");
  }
  mInterval.store(interval, std::memory_order_release);
  mWake.notify_all();
}

void PeriodicSnapshotWorker::run(std::stop_token stopToken) {
  while (!stopToken.stop_requested()) {
    auto const tickStarted = std::chrono::steady_clock::now();
    {
      // This local reference is the tick boundary. A publisher can replace the
      // atomic pointer while mTick runs, but cannot alter or retire this pair.
      auto snapshot = mLatestSnapshot.load(std::memory_order_acquire);
      if (snapshot) {
        mTick(snapshot);
      }
    }

    std::unique_lock lock(mWaitMutex);
    mWake.wait_until(
        lock, tickStarted + mInterval.load(std::memory_order_acquire),
        [&] { return stopToken.stop_requested(); });
  }
}

}  // namespace bw::app
