#include "CpuUpdateProfiler.h"

#include <algorithm>
#include <cmath>

namespace bw::app {
namespace {

constexpr std::size_t index(CpuUpdateSubsystem subsystem) {
  return static_cast<std::size_t>(subsystem);
}

void trimHistory(std::deque<CpuUpdateSample>& samples,
                 double currentTimestampSeconds,
                 double historySeconds) {
  auto const earliestTimestamp = currentTimestampSeconds - historySeconds;
  while (!samples.empty() &&
         samples.front().timestampSeconds < earliestTimestamp) {
    samples.pop_front();
  }
}

}  // namespace

bool CpuUpdateProfiler::captureEnabled() const {
  return mCaptureEnabled;
}

void CpuUpdateProfiler::setCaptureEnabled(bool enabled) {
  if (mCaptureEnabled == enabled) {
    return;
  }

  mCaptureEnabled = enabled;
  mLatestGameUpdateTimestampSeconds = 0.0;
  mLatestSynchronousWorldGenerationNs = 0;
  mSamples.clear();
}

double CpuUpdateProfiler::historySeconds() const {
  return mHistorySeconds;
}

void CpuUpdateProfiler::setHistorySeconds(
    double historySeconds, double currentTimestampSeconds) {
  mHistorySeconds = std::isfinite(historySeconds) && historySeconds > 0.0
                        ? historySeconds
                        : DefaultCpuUpdateHistorySeconds;
  trimHistory(mSamples, currentTimestampSeconds, mHistorySeconds);
}

void CpuUpdateProfiler::setLatestGameUpdate(
    double timestampSeconds,
    std::uint64_t synchronousWorldGenerationNs) {
  if (mCaptureEnabled) {
    mLatestGameUpdateTimestampSeconds = timestampSeconds;
    mLatestSynchronousWorldGenerationNs = synchronousWorldGenerationNs;
  }
}

void CpuUpdateProfiler::recordFrame(std::uint64_t gameNs,
                                    std::uint64_t audioNs) {
  if (!mCaptureEnabled) {
    return;
  }

  CpuUpdateSample sample;
  sample.timestampSeconds = mLatestGameUpdateTimestampSeconds;
  auto const generationNs =
      std::min(gameNs, mLatestSynchronousWorldGenerationNs);
  sample.durationsNs[index(CpuUpdateSubsystem::GameLogic)] =
      gameNs - generationNs;
  sample.durationsNs[index(CpuUpdateSubsystem::SynchronousWorldGeneration)] =
      generationNs;
  sample.durationsNs[index(CpuUpdateSubsystem::Audio)] = audioNs;

  mSamples.push_back(sample);
  mLatestSynchronousWorldGenerationNs = 0;

  trimHistory(
      mSamples, mLatestGameUpdateTimestampSeconds, mHistorySeconds);
}

std::deque<CpuUpdateSample> const& CpuUpdateProfiler::samples() const {
  return mSamples;
}

CpuUpdateProfiler& cpuUpdateProfiler() {
  static CpuUpdateProfiler profiler;
  return profiler;
}

}  // namespace bw::app
