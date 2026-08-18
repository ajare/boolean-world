#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <core/PrimitiveFieldLayout.h>

namespace editor {

inline constexpr float PrimitiveFieldRectangleXyRatio = 2.0f;
inline constexpr float PrimitiveFieldCircleResolution = 0.5f;

enum class PrimitiveFieldType : uint8_t {
  Rectangle,
  Triangle,
  Pentagon,
  Hexagon,
  Circle,
};

struct PrimitiveFieldTypeSelection {
  bool rectangle{true};
  bool triangle{true};
  bool pentagon{true};
  bool hexagon{true};
  bool circle{true};

  [[nodiscard]] bool any() const;
};

struct PrimitiveFieldPrimitivePreview {
  PrimitiveFieldType type{PrimitiveFieldType::Rectangle};
  wp::Vector2 position;
  float fittedSize{0.0f};
  float size{0.0f};
  float angle{0.0f};
  std::vector<wp::Vector2> contour;
};

struct PrimitiveFieldPrimitivePreviewResult {
  std::optional<std::vector<PrimitiveFieldPrimitivePreview>> primitives;
  std::string error;

  [[nodiscard]] bool succeeded() const { return primitives.has_value(); }
};

// Primitive choices and angles use independent PCG-XSH-RR 32 streams derived
// independently from layout sampling. Bounded choice uses rejection sampling;
// angles use the high 24 random bits and an exact power-of-two conversion.
[[nodiscard]] PrimitiveFieldPrimitivePreviewResult buildPrimitiveFieldPreview(
    bw::core::PrimitiveFieldLayout const& layout,
    PrimitiveFieldTypeSelection const& enabledTypes,
    float overlapPercent,
    int32_t placementSeed);

[[nodiscard]] uint32_t effectivePrimitiveFieldMaximum(
    int maximumSites,
    uint32_t existingPrimitiveCount);

struct PrimitiveFieldGenerationIdentity {
  bw::core::PrimitiveFieldExtents worldExtents;
  float minimumSpacing{128.0f};
  int maximumSites{2000};
  uint32_t effectiveMaximumSites{2000};
  int seed{0};
  int lloydIterations{5};

  friend bool operator==(PrimitiveFieldGenerationIdentity const& lhs,
                         PrimitiveFieldGenerationIdentity const& rhs);
};

class PrimitiveFieldGenerationState;

struct PrimitiveFieldPreview {
  bool open{false};
  bool openRequested{false};
  float minimumSpacing{128.0f};
  int maximumSites{2000};
  int seed{0};
  int lloydIterations{5};
  float overlapPercent{10.0f};
  PrimitiveFieldTypeSelection enabledTypes;
  std::optional<bw::core::PrimitiveFieldLayout> layout;
  std::vector<PrimitiveFieldPrimitivePreview> primitives;
  std::string error;

  PrimitiveFieldPreview();
  ~PrimitiveFieldPreview();
  PrimitiveFieldPreview(PrimitiveFieldPreview const&) = delete;
  PrimitiveFieldPreview& operator=(PrimitiveFieldPreview const&) = delete;

  void requestOpen();
  void close();
  // Marks retained preview value data stale without clearing it. Any active
  // request is asked to stop; poll() performs final result coordination.
  void invalidateLayout();
  void refreshPrimitives();
  void generate(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount);
  // Must be called on the editor thread. This is the only path that transfers
  // a completed worker result into preview state.
  void poll(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount);
  void cancelGeneration();
  [[nodiscard]] bool isGenerating() const;
  [[nodiscard]] float generationProgress() const;
  [[nodiscard]] bw::core::PrimitiveFieldLayoutPhase generationPhase() const;
  [[nodiscard]] bool hasCompletePreview() const;

private:
  [[nodiscard]] PrimitiveFieldGenerationIdentity currentIdentity(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount) const;

  std::unique_ptr<PrimitiveFieldGenerationState> mGeneration;
  std::optional<PrimitiveFieldGenerationIdentity> mGeneratedIdentity;
  bool mLayoutCurrent{false};
};

PrimitiveFieldPreview& getPrimitiveFieldPreview();

}  // namespace editor
