#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <willpower/common/Vector2.h>

#include <core/Defines.h>

namespace bw::core {
class World;
}

namespace bw::experiments {
enum struct SampleKind : uint8_t {
  UniformGrid,
  Random,
  NearEdge,
  Count
};

struct GeometryPredicate {
  bool solid{false};
  uint32_t primitiveIndex{~0u};

  bool operator==(GeometryPredicate const& other) const = default;
};

struct GeometryDisagreement {
  wp::Vector2 position;
  SampleKind kind;
  GeometryPredicate oldEngine;
  GeometryPredicate newEngine;
};

struct GeometryComparisonOptions {
  uint32_t gridResolution{32};
  uint32_t randomSampleCount{512};
  uint32_t edgeSamplesPerEdge{3};
  float edgeOffset{0.01f};
  uint32_t randomSeed{0xB001EA5u};
  uint8_t activeLayer{0};
};

struct GeometryComparisonReport {
  std::array<uint32_t, std::size_t(SampleKind::Count)> sampleCounts{};
  double oldSolidArea{0};
  double newSolidArea{0};
  std::vector<GeometryDisagreement> disagreements;

  [[nodiscard]] uint32_t totalSampleCount() const;
  [[nodiscard]] bool matches() const;
};

[[nodiscard]] GeometryComparisonReport CompareWorldGeometry(
    bw::core::World& world,
    GeometryComparisonOptions const& options = {});
}  // namespace bw::experiments
