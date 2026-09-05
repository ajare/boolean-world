#include <iostream>
#include <stdexcept>

#include "CpuUpdateProfiler.h"

namespace {

using bw::app::CpuUpdateProfiler;
using bw::app::CpuUpdateSubsystem;

constexpr std::size_t index(CpuUpdateSubsystem subsystem) {
  return static_cast<std::size_t>(subsystem);
}

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void captureIsDisabledByDefault() {
  CpuUpdateProfiler profiler;
  profiler.recordFrame(20, 30);
  require(!profiler.captureEnabled() && profiler.samples().empty(),
          "CPU update capture was not disabled by default");
}

void separatesSynchronousWorldGenerationFromGameLogic() {
  CpuUpdateProfiler profiler;
  profiler.setCaptureEnabled(true);

  profiler.setLatestGameUpdate(7.0, 30);
  profiler.recordFrame(231, 5);

  auto const& sample = profiler.samples().back();
  auto const& durations = sample.durationsNs;
  require(sample.timestampSeconds == 7.0 &&
              durations[index(CpuUpdateSubsystem::GameLogic)] == 201 &&
              durations[index(
                  CpuUpdateSubsystem::SynchronousWorldGeneration)] == 30 &&
              durations[index(CpuUpdateSubsystem::Audio)] == 5,
          "synchronous World Generation was not separated from game logic");
}

void usesTheConfiguredHistoryAndToggleClearsIt() {
  CpuUpdateProfiler profiler;
  profiler.setCaptureEnabled(true);
  profiler.setHistorySeconds(2.0, 0.0);
  auto recordAt = [&](double timestamp) {
    profiler.setLatestGameUpdate(timestamp, 0);
    profiler.recordFrame(0, 0);
  };
  recordAt(1.0);
  recordAt(2.9);
  recordAt(3.0);
  recordAt(3.01);

  require(profiler.historySeconds() == 2.0 &&
              profiler.samples().size() == 3 &&
              profiler.samples().front().timestampSeconds == 2.9,
          "CPU update capture did not use its configured history interval");

  profiler.setHistorySeconds(0.0, 3.01);
  require(profiler.historySeconds() ==
              bw::app::DefaultCpuUpdateHistorySeconds,
          "invalid history interval did not select the five-second fallback");

  profiler.setCaptureEnabled(false);
  require(profiler.samples().empty(),
          "disabling CPU update capture did not clear its history");
  profiler.setCaptureEnabled(true);
  require(profiler.samples().empty(),
          "enabling CPU update capture restored stale history");
}

}  // namespace

int main() {
  try {
    captureIsDisabledByDefault();
    separatesSynchronousWorldGenerationFromGameLogic();
    usesTheConfiguredHistoryAndToggleClearsIt();
    std::cout << "CPU update profiler regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
