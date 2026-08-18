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

  for (size_t i = 0; i < layout.sites.size(); ++i) {
    auto const& site = layout.sites[i];
    auto const& cell = layout.cells[i];
    if (!finitePoint(site) || cell.vertices.size() < 3) {
      return failure(
          "Every primitive requires a finite site and a complete Voronoi cell.");
    }
    if (!std::all_of(cell.vertices.begin(), cell.vertices.end(), finitePoint)) {
      return failure("Voronoi cell vertices must be finite.");
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

void PrimitiveFieldPreview::requestOpen() {
  open = true;
  openRequested = true;
  minimumSpacing = 128.0f;
  maximumSites = 2000;
  seed = 0;
  lloydIterations = 5;
  overlapPercent = 10.0f;
  enabledTypes = {};
  layout.reset();
  primitives.clear();
  error.clear();
}

void PrimitiveFieldPreview::close() {
  open = false;
  openRequested = false;
  layout.reset();
  primitives.clear();
  error.clear();
}

void PrimitiveFieldPreview::invalidateLayout() {
  layout.reset();
  primitives.clear();
  error.clear();
}

void PrimitiveFieldPreview::refreshPrimitives() {
  if (!layout) {
    primitives.clear();
    error = enabledTypes.any() ? std::string{}
                               : "At least one primitive type must be enabled.";
    return;
  }

  auto result = buildPrimitiveFieldPreview(
      *layout, enabledTypes, overlapPercent, static_cast<int32_t>(seed));
  if (!result.succeeded()) {
    primitives.clear();
    error = std::move(result.error);
    return;
  }
  primitives = std::move(*result.primitives);
  error.clear();
}

void PrimitiveFieldPreview::generate(
    bw::core::PrimitiveFieldExtents const& worldExtents,
    uint32_t existingPrimitiveCount) {
  if (!enabledTypes.any()) {
    error = "At least one primitive type must be enabled.";
    return;
  }
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

  auto primitiveResult = buildPrimitiveFieldPreview(
      *result.layout, enabledTypes, overlapPercent, static_cast<int32_t>(seed));
  if (!primitiveResult.succeeded()) {
    error = std::move(primitiveResult.error);
    return;
  }

  layout = std::move(result.layout);
  primitives = std::move(*primitiveResult.primitives);
  error.clear();
}

bool PrimitiveFieldPreview::hasCompletePreview() const {
  return enabledTypes.any() && error.empty() && layout.has_value() &&
         !layout->sites.empty() && layout->sites.size() == layout->cells.size() &&
         primitives.size() == layout->sites.size();
}

PrimitiveFieldPreview& getPrimitiveFieldPreview() {
  static PrimitiveFieldPreview preview;
  return preview;
}

}  // namespace editor
