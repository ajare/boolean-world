#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <core/Stats.h>

struct ClippingRecord {
  uint32_t clippingId;
  double generationStartedTime{-1.0}, generationCompleteTime{-1.0}, commitedTime{-1.0};
  uint64_t generationTimeNs{0};
  bw::core::Stats stats;

  std::optional<double> commitLag() const {
    if (commitedTime < 0.0 || generationCompleteTime < 0.0) {
      return std::nullopt;
    }
    return commitedTime - generationCompleteTime;
  }

  std::string generationTimeUsText() const {
    return std::to_string(generationTimeNs / 1000);
  }
};