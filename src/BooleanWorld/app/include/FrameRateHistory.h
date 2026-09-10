#pragma once

#include <deque>

namespace bw::app {

struct PresentedFrameRateSample {
  double timestampSeconds{0.0};
  double framesPerSecond{0.0};
};

class PresentedFrameRateHistory {
  static constexpr double HistorySeconds = 5.0;

  std::deque<PresentedFrameRateSample> mSamples;

public:
  void reset();
  void record(double timestampSeconds, double framesPerSecond);

  [[nodiscard]] std::deque<PresentedFrameRateSample> const& samples() const;
};

PresentedFrameRateHistory& presentedFrameRateHistory();

}  // namespace bw::app
