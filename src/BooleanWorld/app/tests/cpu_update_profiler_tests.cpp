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

void breaksSynchronousWorldGenerationIntoPhases() {
  CpuUpdateProfiler profiler;
  profiler.setCaptureEnabled(true);

  bw::app::CpuWorldGenerationTimings generationTimings{
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  profiler.setLatestGameUpdate(7.0, 100, generationTimings);
  profiler.recordFrame(231, 5);

  auto const& sample = profiler.samples().back();
  std::array<std::uint64_t, bw::app::CpuUpdateSubsystemCount> const expected{
      131, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 34, 5};
  require(sample.timestampSeconds == 7.0 &&
              sample.durationsNs == expected,
          "synchronous World Generation phases were not separated from game logic");
}

void clampsGenerationPhasesToMeasuredUpdateTime() {
  CpuUpdateProfiler profiler;
  profiler.setCaptureEnabled(true);

  bw::app::CpuWorldGenerationTimings generationTimings;
  generationTimings.buildPSLGNs = 40;
  generationTimings.cycleExtractionNs = 20;
  profiler.setLatestGameUpdate(1.0, 30, generationTimings);
  profiler.recordFrame(20, 0);

  auto const& durations = profiler.samples().back().durationsNs;
  require(durations[index(CpuUpdateSubsystem::GameLogic)] == 0 &&
              durations[index(CpuUpdateSubsystem::WorldGenerationPSLG)] ==
                  20 &&
              durations[index(CpuUpdateSubsystem::WorldGenerationCycles)] ==
                  0 &&
              durations[index(CpuUpdateSubsystem::WorldGenerationOther)] ==
                  0,
          "World Generation phases exceeded the measured game update");
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
    breaksSynchronousWorldGenerationIntoPhases();
    clampsGenerationPhasesToMeasuredUpdateTime();
    retainsFiveSecondsOfPresentedFrameRates();
    usesTheConfiguredHistoryAndToggleClearsIt();
    std::cout << "CPU update profiler regression passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
