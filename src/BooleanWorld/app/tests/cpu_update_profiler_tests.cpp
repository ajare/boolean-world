#include <iostream>
#include <stdexcept>

#include "CpuUpdateProfiler.h"
#include "FrameRateHistory.h"

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

void retainsFiveSecondsOfPresentedFrameRates() {
  bw::app::PresentedFrameRateHistory history;
  history.record(10.0, 60.0);
  history.record(14.9, 59.5);
  history.record(15.0, 61.0);
  history.record(15.01, 58.0);

  require(history.samples().size() == 3 &&
              history.samples().front().timestampSeconds == 14.9 &&
              history.samples().back().framesPerSecond == 58.0,
          "presented frame-rate history did not retain five seconds");

  history.reset();
  require(history.samples().empty(),
          "presented frame-rate reset retained samples");
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
    retainsFiveSecondsOfPresentedFrameRates();
    usesTheConfiguredHistoryAndToggleClearsIt();
    std::cout << "CPU update profiler regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
