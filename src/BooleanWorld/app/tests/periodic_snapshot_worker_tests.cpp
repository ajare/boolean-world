#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "PeriodicSnapshotWorker.h"

namespace {

using namespace std::chrono_literals;
using bw::app::PeriodicSnapshotWorker;

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

struct WorldSnapshot {
  uint64_t version;
};

struct SceneSnapshot {
  std::shared_ptr<WorldSnapshot const> sourceWorld;
  uint64_t version;
};

struct SnapshotPair {
  std::shared_ptr<WorldSnapshot const> world;
  std::shared_ptr<SceneSnapshot const> scene;
};

std::shared_ptr<SnapshotPair const> makePair(uint64_t version) {
  auto world = std::make_shared<WorldSnapshot const>(version);
  auto scene = std::make_shared<SceneSnapshot const>(world, version);
  return std::make_shared<SnapshotPair const>(
      SnapshotPair{std::move(world), std::move(scene)});
}

void testInFlightTickRetainsItsPair() {
  std::mutex mutex;
  std::condition_variable wake;
  bool oldTickStarted = false;
  bool releaseOldTick = false;
  bool newTickObserved = false;
  std::atomic<bool> mismatched{false};

  PeriodicSnapshotWorker worker(1ms, [&](auto const& erased) {
    auto pair = std::static_pointer_cast<SnapshotPair const>(erased);
    if (pair->world != pair->scene->sourceWorld) {
      mismatched.store(true, std::memory_order_relaxed);
    }

    std::unique_lock lock(mutex);
    if (pair->world->version == 1) {
      oldTickStarted = true;
      wake.notify_all();
      wake.wait(lock, [&] { return releaseOldTick; });
    } else if (pair->world->version == 2) {
      newTickObserved = true;
      wake.notify_all();
    }
  });

  auto oldPair = makePair(1);
  std::weak_ptr<SnapshotPair const> oldLifetime = oldPair;
  worker.publish(oldPair);

  {
    std::unique_lock lock(mutex);
    require(wake.wait_for(lock, 2s, [&] { return oldTickStarted; }),
            "Worker did not start the old-snapshot tick");
  }

  worker.publish(makePair(2));
  oldPair.reset();
  require(!oldLifetime.expired(),
          "Publishing retired a pair while its tick was in flight");

  {
    std::lock_guard lock(mutex);
    releaseOldTick = true;
  }
  wake.notify_all();

  {
    std::unique_lock lock(mutex);
    require(wake.wait_for(lock, 2s, [&] { return newTickObserved; }),
            "Next tick did not pick up the newly published pair");
  }

  for (int attempt = 0; attempt < 200 && !oldLifetime.expired(); ++attempt) {
    std::this_thread::sleep_for(1ms);
  }
  require(oldLifetime.expired(),
          "Old pair remained alive after its in-flight tick completed");
  require(!mismatched.load(std::memory_order_relaxed),
          "Worker observed a scene paired with another World snapshot");
}

void testAggressivePublicationNeverTearsThePair() {
  auto const publishingThread = std::this_thread::get_id();
  std::atomic<bool> mismatched{false};
  std::atomic<bool> ranOnDedicatedThread{false};
  std::atomic<uint64_t> ticks{0};

  PeriodicSnapshotWorker worker(100us, [&](auto const& erased) {
    auto pair = std::static_pointer_cast<SnapshotPair const>(erased);
    if (pair->world != pair->scene->sourceWorld ||
        pair->world->version != pair->scene->version) {
      mismatched.store(true, std::memory_order_relaxed);
    }
    if (std::this_thread::get_id() != publishingThread) {
      ranOnDedicatedThread.store(true, std::memory_order_relaxed);
    }
    ticks.fetch_add(1, std::memory_order_relaxed);
  });

  constexpr uint64_t CommitCount = 50'000;
  for (uint64_t version = 1; version <= CommitCount; ++version) {
    worker.publish(makePair(version));
    if ((version % 64) == 0) std::this_thread::yield();
  }

  auto deadline = std::chrono::steady_clock::now() + 2s;
  while (ticks.load(std::memory_order_relaxed) < 10 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  require(ticks.load(std::memory_order_relaxed) >= 10,
          "Stress test did not execute enough simulation ticks");
  require(!mismatched.load(std::memory_order_relaxed),
          "Aggressive commits tore the World/scene pair");
  require(ranOnDedicatedThread.load(std::memory_order_relaxed),
          "Simulation ticks ran on the publishing game thread");
}

}  // namespace

int main() {
  try {
    testInFlightTickRetainsItsPair();
    testAggressivePublicationNeverTearsThePair();
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
