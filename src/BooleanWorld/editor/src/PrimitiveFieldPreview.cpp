#include "PrimitiveFieldPreview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <core/Defines.h>

namespace editor {
namespace {

class PlacementPcg32 {
  uint64_t mState{0};
  static constexpr uint64_t Multiplier = 6364136223846793005ULL;
  static constexpr uint64_t Increment = 1442695040888963407ULL;

public:
  explicit PlacementPcg32(int32_t seed) {
    next();
    // This derivation gives placement its own stream without consuming or
    // depending on layout-generation random values.
    mState += static_cast<uint32_t>(seed) ^ 0xa511e9b3u;
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
};

bool finitePoint(wp::Vector2 const& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

PrimitiveFieldRectanglePreviewResult failure(std::string message) {
  return {.rectangles = std::nullopt, .error = std::move(message)};
}

PrimitiveFieldRectanglePreview fitRectangle(
    bw::core::PrimitiveFieldCell const& cell,
    wp::Vector2 const& position,
    float angle,
    float overlapPercent) {
  float fittedSize = 0.0f;
  for (auto const& vertex : cell.vertices) {
    auto local = vertex - position;
    local.rotateClockwise(angle);
    fittedSize = std::max(
        fittedSize,
        2.0f * std::max(std::abs(local.x),
                        std::abs(local.y) * PrimitiveFieldRectangleXyRatio));
  }

  auto size = fittedSize * (1.0f + overlapPercent / 100.0f);
  PrimitiveFieldRectanglePreview rectangle{
      .position = position,
      .fittedSize = fittedSize,
      .size = size,
      .angle = angle};

  auto halfWidth = size * 0.5f;
  auto halfHeight = halfWidth / PrimitiveFieldRectangleXyRatio;
  std::array<wp::Vector2, 4> localContour{{
      {halfWidth, halfHeight},
      {-halfWidth, halfHeight},
      {-halfWidth, -halfHeight},
      {halfWidth, -halfHeight},
  }};
  for (size_t i = 0; i < localContour.size(); ++i) {
    localContour[i].rotateAnticlockwise(angle);
    rectangle.contour[i] = position + localContour[i];
  }
  return rectangle;
}

}  // namespace

PrimitiveFieldRectanglePreviewResult buildPrimitiveFieldRectanglePreview(
    bw::core::PrimitiveFieldLayout const& layout,
    float overlapPercent,
    int32_t placementSeed) {
  if (!std::isfinite(overlapPercent) || overlapPercent < 0.0f ||
      overlapPercent > 100.0f) {
    return failure("Overlap must be finite and between 0 and 100 percent.");
  }
  if (layout.sites.empty() || layout.sites.size() != layout.cells.size()) {
    return failure(
        "A complete layout with one Voronoi cell per site is required.");
  }

  std::vector<PrimitiveFieldRectanglePreview> rectangles;
  rectangles.reserve(layout.sites.size());
  PlacementPcg32 random(placementSeed);
  constexpr float AngleQuantum = 360.0f / 16777216.0f;

  for (size_t i = 0; i < layout.sites.size(); ++i) {
    auto const& site = layout.sites[i];
    auto const& cell = layout.cells[i];
    if (!finitePoint(site) || cell.vertices.size() < 3) {
      return failure(
          "Every Rectangle requires a finite site and a complete Voronoi cell.");
    }
    for (auto const& vertex : cell.vertices) {
      if (!finitePoint(vertex)) {
        return failure("Voronoi cell vertices must be finite.");
      }
    }

    auto angle = static_cast<float>(random.next() >> 8u) * AngleQuantum;
    auto rectangle = fitRectangle(cell, site, angle, overlapPercent);
    if (!std::isfinite(rectangle.fittedSize) || rectangle.fittedSize <= 0.0f ||
        !std::isfinite(rectangle.size) || rectangle.size <= 0.0f) {
      return failure("A fitted Rectangle has an invalid uniform size.");
    }
    if (!std::all_of(
            rectangle.contour.begin(), rectangle.contour.end(), finitePoint)) {
      return failure("A fitted Rectangle contour contains non-finite vertices.");
    }
    rectangles.push_back(rectangle);
  }

  return {.rectangles = std::move(rectangles), .error = {}};
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

void PrimitiveFieldPreview::requestOpen() {
  open = true;
  openRequested = true;
  minimumSpacing = 128.0f;
  maximumSites = 2000;
  seed = 0;
  lloydIterations = 5;
  overlapPercent = 10.0f;
  layout.reset();
  rectangles.clear();
  error.clear();
}

void PrimitiveFieldPreview::close() {
  open = false;
  openRequested = false;
  layout.reset();
  rectangles.clear();
  error.clear();
}

void PrimitiveFieldPreview::invalidateLayout() {
  layout.reset();
  rectangles.clear();
  error.clear();
}

void PrimitiveFieldPreview::refreshRectangles() {
  if (!layout) {
    rectangles.clear();
    error.clear();
    return;
  }

  auto result = buildPrimitiveFieldRectanglePreview(
      *layout, overlapPercent, static_cast<int32_t>(seed));
  if (!result.succeeded()) {
    rectangles.clear();
    error = std::move(result.error);
    return;
  }
  rectangles = std::move(*result.rectangles);
  error.clear();
}

void PrimitiveFieldPreview::generate(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) {
  auto effectiveMaximum = effectivePrimitiveFieldMaximum(
      maximumSites, existingPrimitiveCount);
  if (effectiveMaximum == 0) {
    error = "The world has no remaining primitive capacity.";
    return;
  }

  auto result = bw::core::generatePrimitiveFieldLayout(
      {worldExtents, minimumSpacing, effectiveMaximum, seed, lloydIterations});
  if (!result.succeeded()) {
    error = std::move(result.error);
    return;
  }

  auto rectangleResult = buildPrimitiveFieldRectanglePreview(
      *result.layout, overlapPercent, static_cast<int32_t>(seed));
  if (!rectangleResult.succeeded()) {
    error = std::move(rectangleResult.error);
    return;
  }

  layout = std::move(result.layout);
  rectangles = std::move(*rectangleResult.rectangles);
  error.clear();
}

bool PrimitiveFieldPreview::hasCompletePreview() const {
  return error.empty() && layout.has_value() && !layout->sites.empty() &&
         layout->sites.size() == layout->cells.size() &&
         rectangles.size() == layout->sites.size();
}

PrimitiveFieldPreview& getPrimitiveFieldPreview() {
  static PrimitiveFieldPreview preview;
  return preview;
}

}  // namespace editor
