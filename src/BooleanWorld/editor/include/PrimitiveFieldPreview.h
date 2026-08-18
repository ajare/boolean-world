#pragma once

#include <cstddef>
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
  size_t cellIndex{0};
  PrimitiveFieldType type{PrimitiveFieldType::Rectangle};
  bool isHole{false};
  uint8_t regularSideCount{0};
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

// Cell occupancy, primitive choices/angles, and hole chance/shape/angle use
// independent PCG-XSH-RR 32 streams derived independently from layout sampling.
// Bounded choices use rejection sampling; percentages and angles use the high
// 24 random bits.
[[nodiscard]] PrimitiveFieldPrimitivePreviewResult buildPrimitiveFieldPreview(
    bw::core::PrimitiveFieldLayout const& layout,
    PrimitiveFieldTypeSelection const& enabledTypes,
    float occupancyPercent,
    float holeChancePercent,
    float overlapPercent,
    int32_t placementSeed);

// Compatibility overload for consumers that do not request hole primitives.
[[nodiscard]] PrimitiveFieldPrimitivePreviewResult buildPrimitiveFieldPreview(
    bw::core::PrimitiveFieldLayout const& layout,
    PrimitiveFieldTypeSelection const& enabledTypes,
    float occupancyPercent,
    float overlapPercent,
    int32_t placementSeed);

[[nodiscard]] uint32_t effectivePrimitiveFieldMaximum(
    int maximumSites,
    uint32_t existingPrimitiveCount);

struct PrimitiveFieldControlEvaluation {
  uint64_t approximateUncappedSites{0};
  uint32_t remainingWorldCapacity{0};
  uint32_t effectivePlacementCap{0};
  std::string error;

  [[nodiscard]] bool valid() const { return error.empty(); }
};

enum class PrimitiveFieldWorkflowState : uint8_t {
  Idle,
  Generating,
  CurrentPreview,
  StalePreview,
  Cancelled,
  Failed,
  Placing,
  NoCapacity,
};

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
  float occupancyPercent{100.0f};
  float holeChancePercent{0.0f};
  float overlapPercent{10.0f};
  PrimitiveFieldTypeSelection enabledTypes;
  std::optional<bw::core::PrimitiveFieldLayout> layout;
  std::vector<PrimitiveFieldPrimitivePreview> primitives;
  std::string error;
  PrimitiveFieldWorkflowState state{PrimitiveFieldWorkflowState::Idle};

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
  [[nodiscard]] PrimitiveFieldControlEvaluation evaluateControls(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount) const;
  void generate(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount);
  // Must be called on the editor thread. This is the only path that transfers
  // a completed worker result into preview state.
  void poll(
      bw::core::PrimitiveFieldExtents const& worldExtents,
      uint32_t existingPrimitiveCount);
  void cancelGeneration();
  void beginPlacement();
  void finishPlacement(bool succeeded, std::string failure = {});
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
