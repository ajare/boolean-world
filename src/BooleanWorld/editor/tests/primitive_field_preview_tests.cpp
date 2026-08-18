#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <core/Defines.h>

#include "PrimitiveFieldPreview.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bw::core::PrimitiveFieldLayout representativeLayout() {
  return {
      .worldExtents = {{-10.0f, -8.0f}, {10.0f, 8.0f}},
      .sites = {{0.0f, 0.0f}, {-4.0f, 1.0f}, {5.0f, -2.0f}},
      .cells = {
          {{{-3.0f, -2.0f}, {4.0f, -3.0f}, {3.0f, 4.0f}, {-2.0f, 3.0f}}},
          {{{-10.0f, -8.0f}, {0.0f, -8.0f}, {0.0f, 8.0f}, {-10.0f, 8.0f}}},
          {{{0.0f, -8.0f}, {10.0f, -8.0f}, {10.0f, 8.0f}, {0.0f, 8.0f}}},
      }};
}

float cross(wp::Vector2 const& lhs, wp::Vector2 const& rhs) {
  return lhs.x * rhs.y - lhs.y * rhs.x;
}

bool sameCells(
    std::vector<bw::core::PrimitiveFieldCell> const& lhs,
    std::vector<bw::core::PrimitiveFieldCell> const& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i)
    if (lhs[i].vertices != rhs[i].vertices) return false;
  return true;
}

bool contains(
    std::vector<wp::Vector2> const& contour,
    wp::Vector2 const& point) {
  float area = 0.0f;
  for (size_t i = 0; i < contour.size(); ++i)
    area += cross(contour[i], contour[(i + 1) % contour.size()]);
  auto sign = area >= 0.0f ? 1.0f : -1.0f;
  for (size_t i = 0; i < contour.size(); ++i) {
    auto edge = contour[(i + 1) % contour.size()] - contour[i];
    auto relative = point - contour[i];
    auto tolerance = bw::core::PrimitiveFieldNumericTolerance *
                     std::max(1.0f, edge.length());
    if (sign * cross(edge, relative) < -tolerance) return false;
  }
  return true;
}

void waitForGeneration(
    editor::PrimitiveFieldPreview& preview,
    bw::core::PrimitiveFieldExtents const& extents,
    uint32_t existingPrimitiveCount) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (preview.isGenerating() &&
         std::chrono::steady_clock::now() < deadline) {
    preview.poll(extents, existingPrimitiveCount);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  preview.poll(extents, existingPrimitiveCount);
  require(!preview.isGenerating(), "background layout generation timed out");
}

editor::PrimitiveFieldTypeSelection only(editor::PrimitiveFieldType type) {
  editor::PrimitiveFieldTypeSelection selection{
      false, false, false, false, false};
  switch (type) {
    case editor::PrimitiveFieldType::Rectangle: selection.rectangle = true; break;
    case editor::PrimitiveFieldType::Triangle: selection.triangle = true; break;
    case editor::PrimitiveFieldType::Pentagon: selection.pentagon = true; break;
    case editor::PrimitiveFieldType::Hexagon: selection.hexagon = true; break;
    case editor::PrimitiveFieldType::Circle: selection.circle = true; break;
  }
  return selection;
}

void opensWithAllTypeDefaultsRetainsControlsAndRefreshesWithoutRegeneratingLayout() {
  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  require(preview.enabledTypes.rectangle && preview.enabledTypes.triangle &&
              preview.enabledTypes.pentagon && preview.enabledTypes.hexagon &&
              preview.enabledTypes.circle,
          "all eligible primitive types were not enabled by default");

  preview.minimumSpacing = 64.0f;
  preview.maximumSites = 5;
  preview.seed = 11;
  auto extents = bw::core::PrimitiveFieldExtents{
      {-128.0f, -128.0f}, {128.0f, 128.0f}};
  preview.generate(extents, 1);
  require(preview.isGenerating(),
          "layout generation did not dispatch to a background worker");
  waitForGeneration(preview, extents, 1);
  require(preview.hasCompletePreview(), "valid mixed preview generation failed");
  auto sites = preview.layout->sites;
  auto cells = preview.layout->cells;

  preview.enabledTypes = only(editor::PrimitiveFieldType::Triangle);
  preview.refreshPrimitives();
  require(preview.hasCompletePreview() && preview.layout->sites == sites &&
              sameCells(preview.layout->cells, cells),
          "changing enabled types regenerated or invalidated the layout");
  for (auto const& primitive : preview.primitives)
    require(primitive.type == editor::PrimitiveFieldType::Triangle,
            "one remaining enabled type was not used for every site");

  preview.invalidateLayout();
  require(preview.layout && !preview.primitives.empty() &&
              !preview.hasCompletePreview() && !preview.error.empty() &&
              preview.state == editor::PrimitiveFieldWorkflowState::StalePreview,
          "layout invalidation did not retain and identify stale preview data");
  preview.overlapPercent = 17.0f;
  preview.lloydIterations = 4;
  preview.close();
  require(!preview.open && preview.primitives.empty(),
          "closing retained editor overlay geometry");
  preview.requestOpen();
  require(preview.minimumSpacing == 64.0f && preview.maximumSites == 5 &&
              preview.seed == 11 && preview.lloydIterations == 4 &&
              preview.overlapPercent == 17.0f &&
              preview.enabledTypes.triangle &&
              !preview.enabledTypes.rectangle,
          "process-local generator controls did not retain last-used values");
}

void deterministicChoicesAnglesAndMixedSubsets() {
  auto layout = representativeLayout();
  editor::PrimitiveFieldTypeSelection all;
  auto first = editor::buildPrimitiveFieldPreview(layout, all, 0.0f, 73);
  auto repeated = editor::buildPrimitiveFieldPreview(layout, all, 0.0f, 73);
  require(first.succeeded() && repeated.succeeded() &&
              first.primitives->size() == 3,
          "deterministic mixed preview failed");

  std::vector expectedTypes{
      editor::PrimitiveFieldType::Hexagon,
      editor::PrimitiveFieldType::Triangle,
      editor::PrimitiveFieldType::Circle};
  std::vector expectedAngles{
      70.7228546142578125f, 135.0145263671875f, 296.30914306640625f};
  for (size_t i = 0; i < expectedTypes.size(); ++i) {
    auto const& primitive = (*first.primitives)[i];
    auto const& again = (*repeated.primitives)[i];
    require(primitive.type == expectedTypes[i] &&
                primitive.angle == expectedAngles[i] &&
                primitive.type == again.type && primitive.angle == again.angle &&
                primitive.size == again.size,
            "fixed-seed primitive type/angle fixture changed");
  }

  editor::PrimitiveFieldTypeSelection subset{
      true, false, true, false, true};
  auto mixed = editor::buildPrimitiveFieldPreview(layout, subset, 0.0f, 73);
  require(mixed.succeeded() &&
              (*mixed.primitives)[0].type == editor::PrimitiveFieldType::Pentagon &&
              (*mixed.primitives)[1].type == editor::PrimitiveFieldType::Rectangle &&
              (*mixed.primitives)[2].type == editor::PrimitiveFieldType::Rectangle,
          "enabled-subset choice fixture changed");
  for (size_t i = 0; i < expectedAngles.size(); ++i)
    require((*mixed.primitives)[i].angle == expectedAngles[i],
            "type selection perturbed the independent angle stream");
}

void everyEligibleTypeFitsAtGeneratedAnglesAndAppliesOverlap() {
  auto layout = representativeLayout();
  std::vector types{
      editor::PrimitiveFieldType::Rectangle,
      editor::PrimitiveFieldType::Triangle,
      editor::PrimitiveFieldType::Pentagon,
      editor::PrimitiveFieldType::Hexagon,
      editor::PrimitiveFieldType::Circle};
  std::vector<size_t> contourSizes{4, 3, 5, 6, 32};

  for (size_t typeIndex = 0; typeIndex < types.size(); ++typeIndex) {
    auto zero = editor::buildPrimitiveFieldPreview(
        layout, only(types[typeIndex]), 0.0f, 73);
    auto overlap = editor::buildPrimitiveFieldPreview(
        layout, only(types[typeIndex]), 25.0f, 73);
    require(zero.succeeded() && overlap.succeeded(),
            "eligible-type fitting failed");
    for (size_t i = 0; i < layout.cells.size(); ++i) {
      auto const& primitive = (*zero.primitives)[i];
      require(primitive.type == types[typeIndex] &&
                  primitive.contour.size() == contourSizes[typeIndex] &&
                  primitive.angle >= 0.0f && primitive.angle < 360.0f &&
                  (*overlap.primitives)[i].size == primitive.size * 1.25f,
              "type, angle, contour, or post-fit overlap was incorrect");
      for (auto const& vertex : layout.cells[i].vertices)
        require(contains(primitive.contour, vertex),
                "zero-overlap transformed contour did not contain its cell");
    }
  }
}

void completeGenerationIdentityIncludesEveryLayoutInput() {
  editor::PrimitiveFieldGenerationIdentity baseline{
      {{-100.0f, -80.0f}, {120.0f, 90.0f}}, 32.0f, 2000, 1999, 7, 5};
  auto changed = baseline;
  changed.worldExtents.minimum.x -= 1.0f;
  require(changed != baseline, "minimum world extent was omitted from identity");
  changed = baseline;
  changed.worldExtents.maximum.y += 1.0f;
  require(changed != baseline, "maximum world extent was omitted from identity");
  changed = baseline;
  changed.minimumSpacing += 1.0f;
  require(changed != baseline, "spacing was omitted from identity");
  changed = baseline;
  ++changed.maximumSites;
  require(changed != baseline, "requested maximum was omitted from identity");
  changed = baseline;
  --changed.effectiveMaximumSites;
  require(changed != baseline, "effective maximum was omitted from identity");
  changed = baseline;
  ++changed.seed;
  require(changed != baseline, "seed was omitted from identity");
  changed = baseline;
  ++changed.lloydIterations;
  require(changed != baseline, "Lloyd iterations were omitted from identity");
}

void coordinatesStaleSupersededCancelledAndFailedRequests() {
  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  preview.minimumSpacing = 48.0f;
  preview.maximumSites = 120;
  preview.lloydIterations = 2;
  auto extents = bw::core::PrimitiveFieldExtents{
      {-384.0f, -256.0f}, {384.0f, 256.0f}};

  preview.seed = 1;
  preview.generate(extents, 0);
  waitForGeneration(preview, extents, 0);
  require(preview.hasCompletePreview(),
          "baseline background preview did not complete");
  auto baselineSites = preview.layout->sites;
  auto baselineCells = preview.layout->cells;
  auto baselinePrimitives = preview.primitives;

  preview.seed = 2;
  preview.invalidateLayout();
  preview.generate(extents, 0);
  preview.cancelGeneration();
  require(preview.layout->sites == baselineSites &&
              sameCells(preview.layout->cells, baselineCells) &&
              preview.primitives.size() == baselinePrimitives.size() &&
              !preview.hasCompletePreview(),
          "cancellation changed or made current the prior preview");

  preview.generate(extents, 0);
  preview.seed = 3;
  preview.invalidateLayout();
  waitForGeneration(preview, extents, 0);
  require(preview.layout->sites == baselineSites &&
              sameCells(preview.layout->cells, baselineCells) &&
              !preview.hasCompletePreview(),
          "a stale result replaced the prior preview");

  preview.generate(extents, 0);
  preview.seed = 4;
  preview.invalidateLayout();
  preview.generate(extents, 0);
  waitForGeneration(preview, extents, 0);
  require(preview.hasCompletePreview() &&
              preview.layout->sites != baselineSites,
          "the newest superseding request did not become current");
  auto supersedingSites = preview.layout->sites;
  auto supersedingCells = preview.layout->cells;

  preview.minimumSpacing = 7.0f;
  preview.invalidateLayout();
  preview.generate(extents, 0);
  waitForGeneration(preview, extents, 0);
  require(!preview.error.empty() && preview.layout->sites == supersedingSites &&
              sameCells(preview.layout->cells, supersedingCells),
          "background failure was not surfaced while preserving preview data");

  preview.minimumSpacing = 48.0f;
  preview.seed = 5;
  preview.invalidateLayout();
  preview.generate(extents, 0);
  preview.close();
  require(!preview.isGenerating() && !preview.open && !preview.layout,
          "closing the modal retained worker or preview lifetime");
}

void estimatesCountsReportsCapsAndRejectsMalformedLayouts() {
  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  auto extents = bw::core::PrimitiveFieldExtents{
      {-4096.0f, -4096.0f}, {4096.0f, 4096.0f}};
  auto controls = preview.evaluateControls(extents, 1);
  require(controls.valid() && controls.approximateUncappedSites > 2000 &&
              controls.remainingWorldCapacity ==
                  BW_WORLD_PRIMITIVE_COUNT_MAX - 1 &&
              controls.effectivePlacementCap == 2000,
          "live uncapped estimate or distinct capacity counts were incorrect");

  preview.maximumSites = 2;
  preview.minimumSpacing = 64.0f;
  preview.lloydIterations = 0;
  preview.generate({{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  waitForGeneration(preview, {{-128.0f, -128.0f}, {128.0f, 128.0f}}, 1);
  require(preview.hasCompletePreview() &&
              preview.layout->samplingStoppedAtMaximum &&
              preview.layout->sites.size() == 2,
          "sampling did not report stopping at its effective cap");

  auto duplicate = representativeLayout();
  duplicate.sites[1] = duplicate.sites[0];
  require(!editor::buildPrimitiveFieldPreview(duplicate, {}, 10.0f, 0)
               .succeeded(),
          "duplicate retained sites were silently accepted");
  auto malformed = representativeLayout();
  std::swap(malformed.cells[0].vertices[1],
            malformed.cells[0].vertices[2]);
  require(!editor::buildPrimitiveFieldPreview(malformed, {}, 10.0f, 0)
               .succeeded(),
          "a malformed retained cell was silently accepted");
}

void invalidSelectionsOverlapAndCapacityAreRejected() {
  auto layout = representativeLayout();
  editor::PrimitiveFieldTypeSelection none{
      false, false, false, false, false};
  require(!editor::buildPrimitiveFieldPreview(layout, none, 0.0f, 0).succeeded(),
          "a configuration with no enabled types was accepted");
  require(!editor::buildPrimitiveFieldPreview(layout, {}, -0.01f, 0).succeeded() &&
              !editor::buildPrimitiveFieldPreview(
                   layout, {}, std::numeric_limits<float>::infinity(), 0)
                   .succeeded() &&
              !editor::buildPrimitiveFieldPreview(layout, {}, 100.01f, 0)
                   .succeeded(),
          "invalid overlap was accepted");
  require(editor::effectivePrimitiveFieldMaximum(2000, 1) == 2000 &&
              editor::effectivePrimitiveFieldMaximum(
                  2000, BW_WORLD_PRIMITIVE_COUNT_MAX - 7) == 7 &&
              editor::effectivePrimitiveFieldMaximum(
                  2000, BW_WORLD_PRIMITIVE_COUNT_MAX) == 0,
          "effective capacity did not account for existing primitives");

  editor::PrimitiveFieldPreview preview;
  preview.requestOpen();
  auto extents = bw::core::PrimitiveFieldExtents{{-64.0f, -64.0f},
                                                 {64.0f, 64.0f}};
  auto exhausted = preview.evaluateControls(
      extents, BW_WORLD_PRIMITIVE_COUNT_MAX);
  require(!exhausted.valid() && exhausted.remainingWorldCapacity == 0 &&
              exhausted.effectivePlacementCap == 0,
          "exhausted capacity was accepted without actionable evaluation");
  preview.minimumSpacing = std::numeric_limits<float>::infinity();
  require(!preview.evaluateControls(extents, 1).valid(),
          "non-finite spacing passed pre-dispatch validation");
  preview.minimumSpacing = 64.0f;
  preview.maximumSites = BW_WORLD_PRIMITIVE_COUNT_MAX + 1;
  require(!preview.evaluateControls(extents, 1).valid(),
          "out-of-range maximum passed pre-dispatch validation");
}

}  // namespace

int main() {
  try {
    opensWithAllTypeDefaultsRetainsControlsAndRefreshesWithoutRegeneratingLayout();
    deterministicChoicesAnglesAndMixedSubsets();
    everyEligibleTypeFitsAtGeneratedAnglesAndAppliesOverlap();
    completeGenerationIdentityIncludesEveryLayoutInput();
    coordinatesStaleSupersededCancelledAndFailedRequests();
    estimatesCountsReportsCapsAndRejectsMalformedLayouts();
    invalidSelectionsOverlapAndCapacityAreRejected();
    std::cout << "Primitive-field preview state tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
