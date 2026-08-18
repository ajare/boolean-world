#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <core/PrimitiveFieldLayout.h>

namespace editor {

inline constexpr float PrimitiveFieldRectangleXyRatio = 2.0f;

struct PrimitiveFieldRectanglePreview {
  wp::Vector2 position;
  float fittedSize{0.0f};
  float size{0.0f};
  float angle{0.0f};
  std::array<wp::Vector2, 4> contour;
};

struct PrimitiveFieldRectanglePreviewResult {
  std::optional<std::vector<PrimitiveFieldRectanglePreview>> rectangles;
  std::string error;

  [[nodiscard]] bool succeeded() const { return rectangles.has_value(); }
};

// Uses a PCG-XSH-RR 32 stream derived independently from layout sampling.
// Angles use the high 24 random bits and an exact power-of-two conversion to
// degrees, making the stream and [0, 360) conversion platform-independent.
[[nodiscard]] PrimitiveFieldRectanglePreviewResult
buildPrimitiveFieldRectanglePreview(
    bw::core::PrimitiveFieldLayout const& layout,
    float overlapPercent,
    int32_t placementSeed);

[[nodiscard]] uint32_t effectivePrimitiveFieldMaximum(
    int maximumSites,
    uint32_t existingPrimitiveCount);

struct PrimitiveFieldPreview {
  bool open{false};
  bool openRequested{false};
  float minimumSpacing{128.0f};
  int maximumSites{2000};
  int seed{0};
  int lloydIterations{5};
  float overlapPercent{10.0f};
  std::optional<bw::core::PrimitiveFieldLayout> layout;
  std::vector<PrimitiveFieldRectanglePreview> rectangles;
  std::string error;

  void requestOpen();
  void close();
  void invalidateLayout();
  void refreshRectangles();
  void generate(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount);
  [[nodiscard]] bool hasCompletePreview() const;
};

PrimitiveFieldPreview& getPrimitiveFieldPreview();

}  // namespace editor
