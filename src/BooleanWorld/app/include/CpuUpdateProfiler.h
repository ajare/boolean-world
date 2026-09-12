#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace bw::app {

inline constexpr double DefaultCpuUpdateHistorySeconds = 5.0;

enum class CpuUpdateSubsystem : std::size_t {
  GameLogic,
  WorldGenerationPSLG,
  WorldGenerationCycles,
  WorldGenerationHierarchy,
  WorldGenerationClassification,
  WorldGenerationTriangulation,
  WorldGenerationWalls,
  WorldGenerationDetail,
  WorldGenerationLiquid,
  WorldGenerationAccelerationGrids,
  WorldGenerationEmitterCapture,
  WorldGenerationWayfinder,
  WorldGenerationOther,
  Audio,
  Count
};

inline constexpr std::size_t CpuUpdateSubsystemCount =
    static_cast<std::size_t>(CpuUpdateSubsystem::Count);

struct CpuUpdateSample {
  double timestampSeconds{0.0};
  std::array<std::uint64_t, CpuUpdateSubsystemCount> durationsNs{};
};

struct CpuWorldGenerationTimings {
  std::uint64_t buildPSLGNs{0};
  std::uint64_t cycleExtractionNs{0};
  std::uint64_t polygonHierarchyNs{0};
  std::uint64_t classificationNs{0};
  std::uint64_t triangulationNs{0};
  std::uint64_t wallGenerationNs{0};
  std::uint64_t detailGeometryNs{0};
  std::uint64_t liquidEquilibriumNs{0};
  std::uint64_t accelerationGridsNs{0};
  std::uint64_t emitterCaptureNs{0};
  std::uint64_t wayfinderMeshNs{0};
};

class CpuUpdateProfiler {
  bool mCaptureEnabled{false};
  double mHistorySeconds{DefaultCpuUpdateHistorySeconds};
  double mLatestGameUpdateTimestampSeconds{0.0};
  std::uint64_t mLatestSynchronousWorldGenerationNs{0};
  CpuWorldGenerationTimings mLatestWorldGenerationTimings;
  std::deque<CpuUpdateSample> mSamples;

public:
  bool captureEnabled() const;
  void setCaptureEnabled(bool enabled);

  double historySeconds() const;
  void setHistorySeconds(double historySeconds,
                         double currentTimestampSeconds);

  void setLatestGameUpdate(
      double timestampSeconds,
      std::uint64_t synchronousWorldGenerationNs,
      CpuWorldGenerationTimings const& worldGenerationTimings = {});
  void recordFrame(std::uint64_t gameNs, std::uint64_t audioNs);

  std::deque<CpuUpdateSample> const& samples() const;
};

CpuUpdateProfiler& cpuUpdateProfiler();

}  // namespace bw::app
