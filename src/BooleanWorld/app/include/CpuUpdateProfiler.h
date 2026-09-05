#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace bw::app {

inline constexpr double DefaultCpuUpdateHistorySeconds = 5.0;

enum class CpuUpdateSubsystem : std::size_t {
  GameLogic,
  SynchronousWorldGeneration,
  Audio,
  Count
};

inline constexpr std::size_t CpuUpdateSubsystemCount =
    static_cast<std::size_t>(CpuUpdateSubsystem::Count);

struct CpuUpdateSample {
  double timestampSeconds{0.0};
  std::array<std::uint64_t, CpuUpdateSubsystemCount> durationsNs{};
};

class CpuUpdateProfiler {
  bool mCaptureEnabled{false};
  double mHistorySeconds{DefaultCpuUpdateHistorySeconds};
  double mLatestGameUpdateTimestampSeconds{0.0};
  std::uint64_t mLatestSynchronousWorldGenerationNs{0};
  std::deque<CpuUpdateSample> mSamples;

public:
  bool captureEnabled() const;
  void setCaptureEnabled(bool enabled);

  double historySeconds() const;
  void setHistorySeconds(double historySeconds,
                         double currentTimestampSeconds);

  void setLatestGameUpdate(double timestampSeconds,
                           std::uint64_t synchronousWorldGenerationNs);
  void recordFrame(std::uint64_t gameNs, std::uint64_t audioNs);

  std::deque<CpuUpdateSample> const& samples() const;
};

CpuUpdateProfiler& cpuUpdateProfiler();

}  // namespace bw::app
