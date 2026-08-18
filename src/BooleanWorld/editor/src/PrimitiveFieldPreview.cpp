#include "PrimitiveFieldPreview.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include <core/Defines.h>

namespace editor {
namespace {

class PlacementPcg32 {
  uint64_t mState{0};
  static constexpr uint64_t Multiplier = 6364136223846793005ULL;
  static constexpr uint64_t Increment = 1442695040888963407ULL;

public:
  PlacementPcg32(int32_t seed, uint32_t derivation) {
    next();
    mState += static_cast<uint32_t>(seed) ^ derivation;
    next();
  }

  uint32_t next() {
    auto oldState = mState;
    mState = oldState * Multiplier + Increment;
    auto xorshifted =
        static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
    auto rotation = static_cast<uint32_t>(oldState >> 59u);
    return (xorshifted >> rotation) |
           (xorshifted << ((0u - rotation) & 31u));
  }

  uint32_t bounded(uint32_t bound) {
    auto threshold = static_cast<uint32_t>(0u - bound) % bound;
    for (;;) {
      auto value = next();
      if (value >= threshold) {
        return value % bound;
      }
    }
  }
};

bool finitePoint(wp::Vector2 const& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

PrimitiveFieldPrimitivePreviewResult failure(std::string message) {
  return {.primitives = std::nullopt, .error = std::move(message)};
}

std::vector<PrimitiveFieldType> enabledTypeList(
    PrimitiveFieldTypeSelection const& selection) {
  std::vector<PrimitiveFieldType> types;
  if (selection.rectangle) types.push_back(PrimitiveFieldType::Rectangle);
  if (selection.triangle) types.push_back(PrimitiveFieldType::Triangle);
  if (selection.pentagon) types.push_back(PrimitiveFieldType::Pentagon);
  if (selection.hexagon) types.push_back(PrimitiveFieldType::Hexagon);
  if (selection.circle) types.push_back(PrimitiveFieldType::Circle);
  return types;
}

std::vector<wp::Vector2> regularContour(uint32_t sideCount) {
  std::vector<wp::Vector2> contour;
  contour.reserve(sideCount);
  for (uint32_t i = 0; i < sideCount; ++i) {
    contour.push_back(wp::Vector2::UNIT_Y.rotatedClockwiseCopy(
        360.0f * static_cast<float>(i) / static_cast<float>(sideCount)));
  }
  return contour;
}

std::vector<wp::Vector2> unitContour(PrimitiveFieldType type) {
  switch (type) {
    case PrimitiveFieldType::Rectangle:
      return {{1.0f, 1.0f / PrimitiveFieldRectangleXyRatio},
              {-1.0f, 1.0f / PrimitiveFieldRectangleXyRatio},
              {-1.0f, -1.0f / PrimitiveFieldRectangleXyRatio},
              {1.0f, -1.0f / PrimitiveFieldRectangleXyRatio}};
    case PrimitiveFieldType::Triangle:
      return regularContour(3);
    case PrimitiveFieldType::Pentagon:
      return regularContour(5);
    case PrimitiveFieldType::Hexagon:
      return regularContour(6);
    case PrimitiveFieldType::Circle:
      return regularContour(32);
  }
  return {};
}

float cross(wp::Vector2 const& lhs, wp::Vector2 const& rhs) {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

bool validCell(
    bw::core::PrimitiveFieldCell const& cell,
    wp::Vector2 const& site) {
  if (cell.vertices.size() < 3 ||
      !std::all_of(cell.vertices.begin(), cell.vertices.end(), finitePoint)) {
    return false;
  }
  double twiceArea = 0.0;
  for (size_t i = 0; i < cell.vertices.size(); ++i) {
    auto const& a = cell.vertices[i];
    auto const& b = cell.vertices[(i + 1) % cell.vertices.size()];
    if (a == b) return false;
    twiceArea += static_cast<double>(cross(a, b));
  }
  if (!std::isfinite(twiceArea) ||
      twiceArea <= bw::core::PrimitiveFieldNumericTolerance) {
    return false;
  }
  for (size_t i = 0; i < cell.vertices.size(); ++i) {
    auto const& previous =
        cell.vertices[(i + cell.vertices.size() - 1) % cell.vertices.size()];
    auto const& current = cell.vertices[i];
    auto const& next = cell.vertices[(i + 1) % cell.vertices.size()];
    if (cross(current - previous, next - current) <= 0.0f ||
        cross(next - current, site - current) <
            -bw::core::PrimitiveFieldNumericTolerance) {
      return false;
    }
  }
  return true;
}

float fitUniformSize(
    bw::core::PrimitiveFieldCell const& cell,
    wp::Vector2 const& position,
    float angle,
    std::vector<wp::Vector2> const& contour) {
  float twiceArea = 0.0f;
  for (size_t i = 0; i < contour.size(); ++i) {
    twiceArea += cross(contour[i], contour[(i + 1) % contour.size()]);
  }
  auto orientation = twiceArea >= 0.0f ? 1.0f : -1.0f;

  float fittedSize = 0.0f;
  for (auto const& vertex : cell.vertices) {
    auto local = vertex - position;
    local.rotateClockwise(angle);
    for (size_t i = 0; i < contour.size(); ++i) {
      auto const& a = contour[i];
      auto edge = contour[(i + 1) % contour.size()] - a;
      auto support = -orientation * cross(edge, a);
      if (support <= 0.0f || !std::isfinite(support)) {
        return std::numeric_limits<float>::quiet_NaN();
      }
      fittedSize = std::max(
          fittedSize, -orientation * cross(edge, local) / support);
    }
  }
  return fittedSize;
}

PrimitiveFieldPrimitivePreview fitPrimitive(
    bw::core::PrimitiveFieldCell const& cell,
    wp::Vector2 const& position,
    PrimitiveFieldType type,
    float angle,
    float overlapPercent) {
  auto localContour = unitContour(type);
  // Editor size is the primitive's full diameter; unit contour coordinates are
  // transformed by half that value.
  auto fittedSize =
      2.0f * fitUniformSize(cell, position, angle, localContour);
  auto size = fittedSize * (1.0f + overlapPercent / 100.0f);

  PrimitiveFieldPrimitivePreview preview{
      .type = type,
      .position = position,
      .fittedSize = fittedSize,
      .size = size,
      .angle = angle};
  preview.contour.reserve(localContour.size());
  for (auto vertex : localContour) {
    vertex *= size * 0.5f;
    vertex.rotateAnticlockwise(angle);
    preview.contour.push_back(position + vertex);
  }
  return preview;
}

}  // namespace

bool PrimitiveFieldTypeSelection::any() const {
  return rectangle || triangle || pentagon || hexagon || circle;
}

PrimitiveFieldPrimitivePreviewResult buildPrimitiveFieldPreview(
    bw::core::PrimitiveFieldLayout const& layout,
    PrimitiveFieldTypeSelection const& enabledTypes,
    float overlapPercent,
    int32_t placementSeed) {
  if (!enabledTypes.any()) {
    return failure("At least one primitive type must be enabled.");
  }
  if (!std::isfinite(overlapPercent) || overlapPercent < 0.0f ||
      overlapPercent > 100.0f) {
    return failure("Overlap must be finite and between 0 and 100 percent.");
  }
  if (layout.sites.empty() || layout.sites.size() != layout.cells.size()) {
    return failure(
        "A complete layout with one Voronoi cell per site is required.");
  }

  auto types = enabledTypeList(enabledTypes);
  std::vector<PrimitiveFieldPrimitivePreview> primitives;
  primitives.reserve(layout.sites.size());
  PlacementPcg32 choiceRandom(placementSeed, 0x6d2b79f5u);
  PlacementPcg32 angleRandom(placementSeed, 0xa511e9b3u);
  constexpr float AngleQuantum = 360.0f / 16777216.0f;
  std::set<std::pair<float, float>> uniqueSites;

  for (size_t i = 0; i < layout.sites.size(); ++i) {
    auto const& site = layout.sites[i];
    auto const& cell = layout.cells[i];
    if (!finitePoint(site)) {
      return failure("Every retained site must contain finite coordinates.");
    }
    if (!uniqueSites.emplace(site.x, site.y).second) {
      return failure(
          "Retained sites collide after coordinate quantization; increase spacing or change the seed.");
    }
    if (!validCell(cell, site)) {
      return failure(
          "A retained site has a malformed, non-convex, or mismatched Voronoi cell.");
    }

    auto type = types[choiceRandom.bounded(static_cast<uint32_t>(types.size()))];
    auto angle = static_cast<float>(angleRandom.next() >> 8u) * AngleQuantum;
    auto primitive = fitPrimitive(cell, site, type, angle, overlapPercent);
    if (!std::isfinite(primitive.fittedSize) || primitive.fittedSize <= 0.0f ||
        !std::isfinite(primitive.size) || primitive.size <= 0.0f) {
      return failure("A fitted primitive has an invalid uniform size.");
    }
    if (!std::all_of(
            primitive.contour.begin(), primitive.contour.end(), finitePoint)) {
      return failure("A fitted primitive contour contains non-finite vertices.");
    }
    primitives.push_back(std::move(primitive));
  }

  return {.primitives = std::move(primitives), .error = {}};
}

uint32_t effectivePrimitiveFieldMaximum(
    int maximumSites,
    uint32_t existingPrimitiveCount) {
  if (maximumSites <= 0 ||
      existingPrimitiveCount >= BW_WORLD_PRIMITIVE_COUNT_MAX) {
    return 0;
  }
  auto remaining = static_cast<uint32_t>(BW_WORLD_PRIMITIVE_COUNT_MAX) -
                   existingPrimitiveCount;
  return std::min(static_cast<uint32_t>(maximumSites), remaining);
}

bool operator==(PrimitiveFieldGenerationIdentity const& lhs,
                PrimitiveFieldGenerationIdentity const& rhs) {
  auto sameFloat = [](float first, float second) {
    return std::bit_cast<uint32_t>(first) == std::bit_cast<uint32_t>(second);
  };
  return sameFloat(lhs.worldExtents.minimum.x, rhs.worldExtents.minimum.x) &&
         sameFloat(lhs.worldExtents.minimum.y, rhs.worldExtents.minimum.y) &&
         sameFloat(lhs.worldExtents.maximum.x, rhs.worldExtents.maximum.x) &&
         sameFloat(lhs.worldExtents.maximum.y, rhs.worldExtents.maximum.y) &&
         sameFloat(lhs.minimumSpacing, rhs.minimumSpacing) &&
         lhs.maximumSites == rhs.maximumSites &&
         lhs.effectiveMaximumSites == rhs.effectiveMaximumSites &&
         lhs.seed == rhs.seed && lhs.lloydIterations == rhs.lloydIterations;
}

class PrimitiveFieldGenerationState {
public:
  struct Shared {
    uint64_t requestId{};
    PrimitiveFieldGenerationIdentity identity;
    std::atomic<bw::core::PrimitiveFieldLayoutPhase> phase{
        bw::core::PrimitiveFieldLayoutPhase::Sampling};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> done{false};
    std::mutex resultMutex;
    std::optional<bw::core::PrimitiveFieldLayoutResult> result;
  };

  uint64_t nextRequestId{1};
  uint64_t currentRequestId{0};
  std::jthread worker;
  std::shared_ptr<Shared> shared;
};

PrimitiveFieldPreview::PrimitiveFieldPreview()
    : mGeneration(std::make_unique<PrimitiveFieldGenerationState>()) {}

PrimitiveFieldPreview::~PrimitiveFieldPreview() { cancelGeneration(); }

PrimitiveFieldGenerationIdentity PrimitiveFieldPreview::currentIdentity(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) const {
  return {worldExtents,
          minimumSpacing,
          maximumSites,
          effectivePrimitiveFieldMaximum(maximumSites, existingPrimitiveCount),
          seed,
          lloydIterations};
}

void PrimitiveFieldPreview::requestOpen() {
  cancelGeneration();
  open = true;
  openRequested = true;
  // Controls intentionally retain their process-local values. Preview value
  // data belongs to one modal/document lifetime and never survives reopening.
  layout.reset();
  primitives.clear();
  mGeneratedIdentity.reset();
  mLayoutCurrent = false;
  state = PrimitiveFieldWorkflowState::Idle;
  error.clear();
}

void PrimitiveFieldPreview::close() {
  cancelGeneration();
  open = false;
  openRequested = false;
  layout.reset();
  primitives.clear();
  mGeneratedIdentity.reset();
  mLayoutCurrent = false;
  state = PrimitiveFieldWorkflowState::Idle;
  error.clear();
}

void PrimitiveFieldPreview::invalidateLayout() {
  mLayoutCurrent = false;
  state = layout ? PrimitiveFieldWorkflowState::StalePreview
                 : PrimitiveFieldWorkflowState::Idle;
  error = layout ? "Layout inputs changed. Generate Layout again."
                 : std::string{};
  if (mGeneration->worker.joinable()) {
    mGeneration->worker.request_stop();
  }
}

void PrimitiveFieldPreview::refreshPrimitives() {
  if (!layout) {
    primitives.clear();
    error = enabledTypes.any() ? std::string{}
                               : "Enable at least one primitive type.";
    state = error.empty() ? PrimitiveFieldWorkflowState::Idle
                          : PrimitiveFieldWorkflowState::Failed;
    return;
  }

  auto result = buildPrimitiveFieldPreview(
      *layout, enabledTypes, overlapPercent, static_cast<int32_t>(seed));
  if (!result.succeeded()) {
    error = std::move(result.error);
    state = PrimitiveFieldWorkflowState::Failed;
    return;
  }
  primitives = std::move(*result.primitives);
  state = mLayoutCurrent ? PrimitiveFieldWorkflowState::CurrentPreview
                         : PrimitiveFieldWorkflowState::StalePreview;
  error = mLayoutCurrent ? std::string{}
                         : "Layout inputs changed. Generate Layout again.";
}

PrimitiveFieldControlEvaluation PrimitiveFieldPreview::evaluateControls(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) const {
  PrimitiveFieldControlEvaluation evaluation;
  evaluation.remainingWorldCapacity =
      existingPrimitiveCount >= BW_WORLD_PRIMITIVE_COUNT_MAX
          ? 0
          : static_cast<uint32_t>(BW_WORLD_PRIMITIVE_COUNT_MAX) -
                existingPrimitiveCount;
  evaluation.effectivePlacementCap = effectivePrimitiveFieldMaximum(
      maximumSites, existingPrimitiveCount);

  if (!enabledTypes.any()) {
    evaluation.error = "Enable at least one primitive type.";
    return evaluation;
  }
  if (!std::isfinite(overlapPercent) || overlapPercent < 0.0f ||
      overlapPercent > 100.0f) {
    evaluation.error = "Overlap must be finite and between 0 and 100 percent.";
    return evaluation;
  }
  if (maximumSites < 1 || maximumSites > BW_WORLD_PRIMITIVE_COUNT_MAX) {
    evaluation.error = "Requested maximum must be between 1 and the engine limit.";
    return evaluation;
  }
  if (lloydIterations < 0 || lloydIterations > 20) {
    evaluation.error = "Lloyd iterations must be between 0 and 20.";
    return evaluation;
  }
  auto estimate = bw::core::estimatePrimitiveFieldSiteCount(
      worldExtents, minimumSpacing);
  if (!estimate.succeeded()) {
    evaluation.error = std::move(estimate.error);
    return evaluation;
  }
  evaluation.approximateUncappedSites = *estimate.uncappedSiteCount;
  if (evaluation.remainingWorldCapacity == 0) {
    evaluation.error =
        "No world capacity remains. Delete primitives before generating a field.";
  }
  return evaluation;
}

void PrimitiveFieldPreview::generate(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) {
  auto controls = evaluateControls(worldExtents, existingPrimitiveCount);
  if (!controls.valid()) {
    error = std::move(controls.error);
    state = controls.remainingWorldCapacity == 0
                ? PrimitiveFieldWorkflowState::NoCapacity
                : PrimitiveFieldWorkflowState::Failed;
    return;
  }
  auto identity = currentIdentity(worldExtents, existingPrimitiveCount);

  cancelGeneration();
  mLayoutCurrent = mGeneratedIdentity && *mGeneratedIdentity == identity;
  state = PrimitiveFieldWorkflowState::Generating;
  error.clear();

  auto shared = std::make_shared<PrimitiveFieldGenerationState::Shared>();
  shared->requestId = mGeneration->nextRequestId++;
  mGeneration->currentRequestId = shared->requestId;
  shared->identity = identity;
  mGeneration->shared = shared;
  auto request = bw::core::PrimitiveFieldLayoutRequest{
      identity.worldExtents, identity.minimumSpacing,
      identity.effectiveMaximumSites, identity.seed, identity.lloydIterations};
  try {
    mGeneration->worker = std::jthread(
        [shared, request](std::stop_token stopToken) {
          auto result = bw::core::generatePrimitiveFieldLayout(
              request,
              {stopToken,
               [shared](bw::core::PrimitiveFieldLayoutProgress const& value) {
                 shared->phase.store(value.phase, std::memory_order_relaxed);
                 shared->progress.store(value.completion,
                                        std::memory_order_relaxed);
               }});
          {
            std::lock_guard lock(shared->resultMutex);
            shared->result.emplace(std::move(result));
          }
          shared->done.store(true, std::memory_order_release);
        });
  } catch (std::exception const& exception) {
    mGeneration->shared.reset();
    state = PrimitiveFieldWorkflowState::Failed;
    error = std::string("Could not start layout generation: ") + exception.what();
  }
}

void PrimitiveFieldPreview::poll(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) {
  auto controls = evaluateControls(worldExtents, existingPrimitiveCount);
  auto identity = currentIdentity(worldExtents, existingPrimitiveCount);
  mLayoutCurrent = mGeneratedIdentity && *mGeneratedIdentity == identity;
  if (controls.remainingWorldCapacity == 0) {
    state = PrimitiveFieldWorkflowState::NoCapacity;
    error = std::move(controls.error);
  } else if (!mGeneration->shared && state != PrimitiveFieldWorkflowState::Failed &&
             state != PrimitiveFieldWorkflowState::Cancelled &&
             state != PrimitiveFieldWorkflowState::Placing) {
    state = mLayoutCurrent ? PrimitiveFieldWorkflowState::CurrentPreview
                           : (layout ? PrimitiveFieldWorkflowState::StalePreview
                                     : PrimitiveFieldWorkflowState::Idle);
  }

  auto shared = mGeneration->shared;
  if (!shared) {
    return;
  }
  if (!(shared->identity == identity) && mGeneration->worker.joinable()) {
    mGeneration->worker.request_stop();
  }
  if (!shared->done.load(std::memory_order_acquire)) {
    return;
  }

  if (mGeneration->worker.joinable()) {
    mGeneration->worker.join();
  }
  std::optional<bw::core::PrimitiveFieldLayoutResult> result;
  {
    std::lock_guard lock(shared->resultMutex);
    result = std::move(shared->result);
  }
  mGeneration->shared.reset();

  if (shared->requestId != mGeneration->currentRequestId ||
      !(shared->identity == identity) || !result || result->cancelled()) {
    return;
  }
  if (!result->succeeded()) {
    state = PrimitiveFieldWorkflowState::Failed;
    error = result->error.empty()
                ? "Layout generation failed without an error message."
                : std::move(result->error);
    return;
  }

  auto primitiveResult = buildPrimitiveFieldPreview(
      *result->layout, enabledTypes, overlapPercent,
      static_cast<int32_t>(identity.seed));
  if (!primitiveResult.succeeded()) {
    state = PrimitiveFieldWorkflowState::Failed;
    error = std::move(primitiveResult.error);
    return;
  }

  layout = std::move(result->layout);
  primitives = std::move(*primitiveResult.primitives);
  mGeneratedIdentity = identity;
  mLayoutCurrent = true;
  state = PrimitiveFieldWorkflowState::CurrentPreview;
  error.clear();
}

void PrimitiveFieldPreview::cancelGeneration() {
  auto wasGenerating = isGenerating();
  if (mGeneration->worker.joinable()) {
    mGeneration->worker.request_stop();
    mGeneration->worker.join();
  }
  mGeneration->shared.reset();
  mGeneration->currentRequestId = 0;
  if (wasGenerating && open) {
    state = PrimitiveFieldWorkflowState::Cancelled;
    error = "Layout generation cancelled; the previous preview was preserved.";
  }
}

void PrimitiveFieldPreview::beginPlacement() {
  state = PrimitiveFieldWorkflowState::Placing;
  error.clear();
}

void PrimitiveFieldPreview::finishPlacement(bool succeeded, std::string failure) {
  if (succeeded) {
    state = PrimitiveFieldWorkflowState::CurrentPreview;
    error.clear();
  } else {
    state = PrimitiveFieldWorkflowState::Failed;
    error = failure.empty() ? "Primitive placement failed." : std::move(failure);
  }
}

bool PrimitiveFieldPreview::isGenerating() const {
  return mGeneration->worker.joinable() && mGeneration->shared != nullptr;
}

float PrimitiveFieldPreview::generationProgress() const {
  return mGeneration->shared
             ? mGeneration->shared->progress.load(std::memory_order_relaxed)
             : 0.0f;
}

bw::core::PrimitiveFieldLayoutPhase
PrimitiveFieldPreview::generationPhase() const {
  return mGeneration->shared
             ? mGeneration->shared->phase.load(std::memory_order_relaxed)
             : bw::core::PrimitiveFieldLayoutPhase::Sampling;
}

bool PrimitiveFieldPreview::hasCompletePreview() const {
  return mLayoutCurrent && enabledTypes.any() && layout.has_value() &&
         !layout->sites.empty() &&
         layout->sites.size() == layout->cells.size() &&
         primitives.size() == layout->sites.size();
}

PrimitiveFieldPreview& getPrimitiveFieldPreview() {
  static PrimitiveFieldPreview preview;
  return preview;
}

}  // namespace editor
