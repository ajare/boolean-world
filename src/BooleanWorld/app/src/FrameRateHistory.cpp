#include "FrameRateHistory.h"

#include <cmath>

namespace bw::app {

void PresentedFrameRateHistory::reset() {
  mSamples.clear();
}

void PresentedFrameRateHistory::record(
    double timestampSeconds, double framesPerSecond) {
  if (!std::isfinite(timestampSeconds) ||
      !std::isfinite(framesPerSecond) || framesPerSecond < 0.0) {
    return;
  }
  if (!mSamples.empty() &&
      timestampSeconds <= mSamples.back().timestampSeconds) {
    return;
  }

  mSamples.push_back({timestampSeconds, framesPerSecond});
  auto const historyStart = timestampSeconds - HistorySeconds;
  while (!mSamples.empty() &&
         mSamples.front().timestampSeconds < historyStart) {
    mSamples.pop_front();
  }
}

std::deque<PresentedFrameRateSample> const&
PresentedFrameRateHistory::samples() const {
  return mSamples;
}

PresentedFrameRateHistory& presentedFrameRateHistory() {
  static PresentedFrameRateHistory history;
  return history;
}

}  // namespace bw::app
