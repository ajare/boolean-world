#define NOMINMAX

#include <core/PrimitiveFieldLayout.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <core/Defines.h>

#pragma warning(push)
#pragma warning(disable : 4244 4267 4706)
#define JC_VORONOI_IMPLEMENTATION
#include <jc_voronoi/jc_voronoi.h>
#pragma warning(pop)

namespace bw::core {
namespace {

constexpr int64_t CoordinateScale = 4096;
constexpr int CandidateCountPerSite = 30;
constexpr int64_t MaximumExtentSpanUnits = int64_t{1} << 30;

struct IntPoint {
  int64_t x{};
  int64_t y{};

  friend bool operator==(IntPoint const&, IntPoint const&) = default;
};

struct Candidate {
  IntPoint point;
  int64_t centreDistanceSquared{};
  uint64_t sequence{};
};

struct CandidateFartherFirst {
  bool operator()(Candidate const& lhs, Candidate const& rhs) const {
    if (lhs.centreDistanceSquared != rhs.centreDistanceSquared) {
      return lhs.centreDistanceSquared > rhs.centreDistanceSquared;
    }
    if (lhs.point.x != rhs.point.x) {
      return lhs.point.x > rhs.point.x;
    }
    if (lhs.point.y != rhs.point.y) {
      return lhs.point.y > rhs.point.y;
    }
    return lhs.sequence > rhs.sequence;
  }
};

class Pcg32 {
  uint64_t mState{0};
  static constexpr uint64_t Multiplier = 6364136223846793005ULL;
  static constexpr uint64_t Increment = 1442695040888963407ULL;

public:
  explicit Pcg32(int32_t seed) {
    next();
    mState += static_cast<uint32_t>(seed);
    next();
  }

  uint32_t next() {
    auto oldState = mState;
    mState = oldState * Multiplier + Increment;
    auto xorshifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
    auto rotation = static_cast<uint32_t>(oldState >> 59u);
    return (xorshifted >> rotation) |
           (xorshifted << ((0u - rotation) & 31u));
  }
};

PrimitiveFieldLayoutResult failure(std::string message) {
  return {.layout = std::nullopt,
          .error = std::move(message),
          .wasCancelled = false};
}

PrimitiveFieldLayoutResult cancelled() {
  return {.layout = std::nullopt, .error = {}, .wasCancelled = true};
}

class ExecutionContext {
  PrimitiveFieldLayoutExecution const* mExecution{};
  float mLastCompletion{0.0f};

public:
  explicit ExecutionContext(PrimitiveFieldLayoutExecution const* execution)
      : mExecution(execution) {}

  [[nodiscard]] bool cancellationRequested() const {
    return mExecution && mExecution->stopToken.stop_requested();
  }

  bool report(PrimitiveFieldLayoutPhase phase, float completion) {
    completion = std::clamp(completion, mLastCompletion, 1.0f);
    mLastCompletion = completion;
    if (mExecution && mExecution->reportProgress) {
      mExecution->reportProgress({phase, completion});
    }
    return cancellationRequested();
  }
};

bool finitePoint(wp::Vector2 const& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

bool convertToGrid(float value, int64_t& result) {
  if (!std::isfinite(value)) {
    return false;
  }
  auto scaled = static_cast<double>(value) * static_cast<double>(CoordinateScale);
  constexpr auto limit = static_cast<double>(std::numeric_limits<int64_t>::max() / 2);
  if (scaled < -limit || scaled > limit) {
    return false;
  }
  result = scaled >= 0.0
               ? static_cast<int64_t>(std::floor(scaled + 0.5))
               : static_cast<int64_t>(std::ceil(scaled - 0.5));
  return true;
}

float fromGrid(int64_t value) {
  return static_cast<float>(static_cast<double>(value) /
                            static_cast<double>(CoordinateScale));
}

int64_t squaredDistance(IntPoint const& lhs, IntPoint const& rhs) {
  auto dx = lhs.x - rhs.x;
  auto dy = lhs.y - rhs.y;
  return dx * dx + dy * dy;
}

int64_t floorDivide(int64_t value, int64_t divisor) {
  auto quotient = value / divisor;
  auto remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

int64_t roundedDivide(int64_t numerator, int64_t denominator) {
  if (numerator >= 0) {
    return (numerator + denominator / 2) / denominator;
  }
  return -((-numerator + denominator / 2) / denominator);
}

bool validExtents(
    PrimitiveFieldExtents const& extents,
    IntPoint& minimum,
    IntPoint& maximum,
    std::string& error) {
  if (!finitePoint(extents.minimum) || !finitePoint(extents.maximum)) {
    error = "World extents must contain only finite coordinates.";
    return false;
  }
  if (!convertToGrid(extents.minimum.x, minimum.x) ||
      !convertToGrid(extents.minimum.y, minimum.y) ||
      !convertToGrid(extents.maximum.x, maximum.x) ||
      !convertToGrid(extents.maximum.y, maximum.y)) {
    error = "World extents are outside the supported coordinate range.";
    return false;
  }
  if (minimum.x >= maximum.x || minimum.y >= maximum.y) {
    error = "World extents must have positive width and height.";
    return false;
  }
  if (maximum.x - minimum.x > MaximumExtentSpanUnits ||
      maximum.y - minimum.y > MaximumExtentSpanUnits) {
    error = "World extents are too large for deterministic layout generation.";
    return false;
  }
  return true;
}

wp::Vector2 canonicalPoint(jcv_point const& point,
                           PrimitiveFieldExtents const& extents) {
  auto canonicalCoordinate = [](float value, float minimum, float maximum) {
    if (std::abs(value - minimum) <= PrimitiveFieldNumericTolerance * 2.0f) {
      return minimum;
    }
    if (std::abs(value - maximum) <= PrimitiveFieldNumericTolerance * 2.0f) {
      return maximum;
    }
    int64_t units = 0;
    if (!convertToGrid(value, units)) {
      return std::numeric_limits<float>::quiet_NaN();
    }
    return fromGrid(units);
  };

  return {canonicalCoordinate(point.x, extents.minimum.x, extents.maximum.x),
          canonicalCoordinate(point.y, extents.minimum.y, extents.maximum.y)};
}

bool samePoint(wp::Vector2 const& lhs, wp::Vector2 const& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

double signedArea(std::vector<wp::Vector2> const& vertices) {
  double twiceArea = 0.0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& current = vertices[i];
    auto const& next = vertices[(i + 1) % vertices.size()];
    twiceArea += static_cast<double>(current.x) * next.y -
                 static_cast<double>(current.y) * next.x;
  }
  return twiceArea * 0.5;
}

struct DiagramOwner {
  jcv_diagram diagram{};

  ~DiagramOwner() { jcv_diagram_free(&diagram); }
};

PrimitiveFieldLayoutResult buildBoundedLayout(
    PrimitiveFieldExtents const& worldExtents,
    std::vector<wp::Vector2> sites,
    ExecutionContext* execution = nullptr,
    bool reportFinalPhases = false) {
  if (execution && execution->cancellationRequested()) {
    return cancelled();
  }
  if (reportFinalPhases &&
      execution->report(PrimitiveFieldLayoutPhase::VoronoiConstruction,
                        0.65f)) {
    return cancelled();
  }
  IntPoint extentMinimum;
  IntPoint extentMaximum;
  std::string extentError;
  if (!validExtents(worldExtents, extentMinimum, extentMaximum, extentError)) {
    return failure(std::move(extentError));
  }
  if (sites.empty()) {
    return failure("At least one site is required to construct a Voronoi layout.");
  }
  if (sites.size() > BW_WORLD_PRIMITIVE_COUNT_MAX) {
    return failure("Site count exceeds the engine primitive limit.");
  }

  std::vector<IntPoint> canonicalSites;
  canonicalSites.reserve(sites.size());
  for (auto& site : sites) {
    if (execution && execution->cancellationRequested()) {
      return cancelled();
    }
    if (!finitePoint(site)) {
      return failure("Every Voronoi site must contain finite coordinates.");
    }
    IntPoint point;
    if (!convertToGrid(site.x, point.x) || !convertToGrid(site.y, point.y)) {
      return failure("A Voronoi site is outside the supported coordinate range.");
    }
    site = {fromGrid(point.x), fromGrid(point.y)};
    if (site.x < worldExtents.minimum.x || site.x > worldExtents.maximum.x ||
        site.y < worldExtents.minimum.y || site.y > worldExtents.maximum.y) {
      return failure("Every Voronoi site must lie inside the world extents.");
    }
    canonicalSites.push_back(point);
  }

  auto centreTwice = IntPoint{extentMinimum.x + extentMaximum.x,
                              extentMinimum.y + extentMaximum.y};
  auto centreDistance = [&](IntPoint const& point) {
    auto dx = point.x * 2 - centreTwice.x;
    auto dy = point.y * 2 - centreTwice.y;
    return dx * dx + dy * dy;
  };
  std::sort(canonicalSites.begin(), canonicalSites.end(),
            [&](auto const& lhs, auto const& rhs) {
              auto lhsDistance = centreDistance(lhs);
              auto rhsDistance = centreDistance(rhs);
              if (lhsDistance != rhsDistance) {
                return lhsDistance < rhsDistance;
              }
              if (lhs.x != rhs.x) {
                return lhs.x < rhs.x;
              }
              return lhs.y < rhs.y;
            });
  sites.clear();
  sites.reserve(canonicalSites.size());
  for (auto const& point : canonicalSites) {
    sites.push_back({fromGrid(point.x), fromGrid(point.y)});
  }

  auto duplicateCheck = canonicalSites;
  std::sort(duplicateCheck.begin(), duplicateCheck.end(), [](auto const& lhs, auto const& rhs) {
    return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
  });
  if (std::adjacent_find(duplicateCheck.begin(), duplicateCheck.end()) !=
      duplicateCheck.end()) {
    return failure("Duplicate Voronoi sites are not allowed (including sites that coincide on the public coordinate grid).");
  }

  std::vector<jcv_point> inputPoints;
  inputPoints.reserve(sites.size());
  for (auto const& site : sites) {
    inputPoints.push_back({site.x, site.y});
  }

  auto clippingRectangle = jcv_rect{
      {worldExtents.minimum.x, worldExtents.minimum.y},
      {worldExtents.maximum.x, worldExtents.maximum.y}};
  DiagramOwner owner;
  if (execution && execution->cancellationRequested()) {
    return cancelled();
  }
  jcv_diagram_generate(static_cast<int>(inputPoints.size()), inputPoints.data(),
                       &clippingRectangle, nullptr, &owner.diagram);
  if (execution && execution->cancellationRequested()) {
    return cancelled();
  }
  if (!owner.diagram.internal) {
    return failure("Voronoi tessellation failed to allocate a diagram.");
  }
  if (owner.diagram.numsites != static_cast<int>(sites.size())) {
    return failure("Voronoi tessellation rejected one or more sites; check for duplicate or degenerate input.");
  }

  PrimitiveFieldLayout layout;
  layout.worldExtents = worldExtents;
  layout.sites = std::move(sites);
  layout.cells.resize(layout.sites.size());
  std::vector<bool> assigned(layout.sites.size(), false);

  auto const* diagramSites = jcv_diagram_get_sites(&owner.diagram);
  for (int diagramIndex = 0; diagramIndex < owner.diagram.numsites; ++diagramIndex) {
    if (execution && execution->cancellationRequested()) {
      return cancelled();
    }
    if (reportFinalPhases &&
        execution->report(
            PrimitiveFieldLayoutPhase::VoronoiConstruction,
            0.65f + 0.25f * static_cast<float>(diagramIndex + 1) /
                        static_cast<float>(owner.diagram.numsites))) {
      return cancelled();
    }
    auto const& diagramSite = diagramSites[diagramIndex];
    auto siteIndex = static_cast<size_t>(diagramSite.index);
    if (siteIndex >= layout.cells.size() || assigned[siteIndex]) {
      return failure("Voronoi tessellation returned an invalid or repeated site index.");
    }

    jcv_edge_iter iterator{};
    jcv_site_get_edges(&owner.diagram, &diagramSite, &iterator);
    std::vector<wp::Vector2> starts;
    std::vector<wp::Vector2> ends;
    jcv_edge edge{};
    while (jcv_edge_next(&iterator, &edge)) {
      if (execution && execution->cancellationRequested()) {
        return cancelled();
      }
      auto start = canonicalPoint(edge.pos[0], worldExtents);
      auto end = canonicalPoint(edge.pos[1], worldExtents);
      if (!finitePoint(start) || !finitePoint(end)) {
        return failure("Voronoi tessellation produced a non-finite cell edge.");
      }
      if (start.x < worldExtents.minimum.x || start.x > worldExtents.maximum.x ||
          start.y < worldExtents.minimum.y || start.y > worldExtents.maximum.y ||
          end.x < worldExtents.minimum.x || end.x > worldExtents.maximum.x ||
          end.y < worldExtents.minimum.y || end.y > worldExtents.maximum.y) {
        return failure("Voronoi tessellation produced an edge outside the world extents.");
      }
      if (samePoint(start, end)) {
        return failure("Voronoi tessellation produced a degenerate zero-length cell edge.");
      }
      starts.push_back(start);
      ends.push_back(end);
    }

    if (starts.size() < 3) {
      return failure("Voronoi tessellation produced a cell with fewer than three edges.");
    }
    for (size_t edgeIndex = 0; edgeIndex < starts.size(); ++edgeIndex) {
      if (!samePoint(ends[edgeIndex], starts[(edgeIndex + 1) % starts.size()])) {
        return failure("Voronoi tessellation produced a malformed open cell boundary.");
      }
    }
    if (!(signedArea(starts) > 0.0)) {
      return failure("Voronoi tessellation produced a degenerate or clockwise cell.");
    }

    auto first = std::min_element(starts.begin(), starts.end(), [](auto const& lhs, auto const& rhs) {
      return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
    });
    std::rotate(starts.begin(), first, starts.end());
    layout.cells[siteIndex].vertices = std::move(starts);
    assigned[siteIndex] = true;
  }

  if (std::find(assigned.begin(), assigned.end(), false) != assigned.end()) {
    return failure("Voronoi tessellation did not produce exactly one cell for every site.");
  }

  if (reportFinalPhases &&
      execution->report(PrimitiveFieldLayoutPhase::Validation, 0.90f)) {
    return cancelled();
  }
  double cellArea = 0.0;
  for (size_t cellIndex = 0; cellIndex < layout.cells.size(); ++cellIndex) {
    if (execution && execution->cancellationRequested()) {
      return cancelled();
    }
    cellArea += signedArea(layout.cells[cellIndex].vertices);
    if (reportFinalPhases &&
        execution->report(
            PrimitiveFieldLayoutPhase::Validation,
            0.90f + 0.08f * static_cast<float>(cellIndex + 1) /
                        static_cast<float>(layout.cells.size()))) {
      return cancelled();
    }
  }
  auto worldWidth = static_cast<double>(worldExtents.maximum.x) -
                    worldExtents.minimum.x;
  auto worldHeight = static_cast<double>(worldExtents.maximum.y) -
                     worldExtents.minimum.y;
  auto worldArea = worldWidth * worldHeight;
  auto areaTolerance = std::max(
      worldArea * 1.0e-5,
      static_cast<double>(PrimitiveFieldNumericTolerance) *
          (worldWidth + worldHeight) * layout.cells.size() * 2.0);
  if (std::abs(cellArea - worldArea) > areaTolerance) {
    return failure("Voronoi cells do not form a complete bounded tessellation of the world extents.");
  }

  return {.layout = std::move(layout), .error = {}, .wasCancelled = false};
}

bool cellCentroid(PrimitiveFieldCell const& cell,
                  IntPoint const& worldMinimum,
                  IntPoint& centroid) {
  if (cell.vertices.size() < 3) {
    return false;
  }

  std::vector<IntPoint> vertices;
  vertices.reserve(cell.vertices.size());
  for (auto const& vertex : cell.vertices) {
    IntPoint point;
    if (!finitePoint(vertex) || !convertToGrid(vertex.x, point.x) ||
        !convertToGrid(vertex.y, point.y)) {
      return false;
    }
    vertices.push_back({point.x - worldMinimum.x, point.y - worldMinimum.y});
  }

  double twiceArea = 0.0;
  double xMoment = 0.0;
  double yMoment = 0.0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& current = vertices[i];
    auto const& next = vertices[(i + 1) % vertices.size()];
    auto cross = static_cast<double>(current.x) * next.y -
                 static_cast<double>(next.x) * current.y;
    twiceArea += cross;
    xMoment += static_cast<double>(current.x + next.x) * cross;
    yMoment += static_cast<double>(current.y + next.y) * cross;
  }
  if (!std::isfinite(twiceArea) || !std::isfinite(xMoment) ||
      !std::isfinite(yMoment) || !(twiceArea > 0.0)) {
    return false;
  }

  auto x = xMoment / (3.0 * twiceArea);
  auto y = yMoment / (3.0 * twiceArea);
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
      x > static_cast<double>(MaximumExtentSpanUnits) ||
      y > static_cast<double>(MaximumExtentSpanUnits)) {
    return false;
  }
  centroid = {
      worldMinimum.x + static_cast<int64_t>(std::floor(x + 0.5)),
      worldMinimum.y + static_cast<int64_t>(std::floor(y + 0.5))};
  return true;
}

PrimitiveFieldLayoutResult relaxLayout(
    PrimitiveFieldLayout layout,
    IntPoint const& worldMinimum,
    IntPoint const& domainMinimum,
    IntPoint const& domainMaximum,
    int64_t spacing,
    int32_t iterations,
    ExecutionContext& execution) {
  auto const retainedSiteCount = layout.sites.size();
  auto const minimumDistanceSquared = spacing * spacing;

  for (int32_t iteration = 0; iteration < iterations; ++iteration) {
    if (execution.cancellationRequested()) {
      return cancelled();
    }
    if (layout.cells.size() != retainedSiteCount ||
        layout.sites.size() != retainedSiteCount) {
      return failure("Lloyd relaxation changed the retained site count.");
    }

    std::vector<IntPoint> retainedSites;
    retainedSites.reserve(retainedSiteCount);
    for (auto const& site : layout.sites) {
      if (execution.cancellationRequested()) {
        return cancelled();
      }
      IntPoint point;
      if (!finitePoint(site) || !convertToGrid(site.x, point.x) ||
          !convertToGrid(site.y, point.y)) {
        return failure("Lloyd relaxation encountered an invalid retained site.");
      }
      retainedSites.push_back(point);
    }

    // The layout's center-distance/x/y ordering is stable. Earlier decisions
    // are visible to later proposals; sites not yet visited retain their old
    // positions until their turn.
    for (size_t siteIndex = 0; siteIndex < retainedSiteCount; ++siteIndex) {
      auto lloydCompletion =
          (static_cast<float>(iteration) +
           static_cast<float>(siteIndex) /
               static_cast<float>(retainedSiteCount)) /
          static_cast<float>(iterations);
      if (execution.report(PrimitiveFieldLayoutPhase::LloydRelaxation,
                           0.25f + 0.40f * lloydCompletion)) {
        return cancelled();
      }
      IntPoint proposed;
      if (!cellCentroid(layout.cells[siteIndex], worldMinimum, proposed)) {
        return failure("Lloyd relaxation could not calculate a valid bounded-cell centroid.");
      }
      if (proposed.x < domainMinimum.x || proposed.x > domainMaximum.x ||
          proposed.y < domainMinimum.y || proposed.y > domainMaximum.y) {
        continue;
      }

      bool preservesSpacing = true;
      for (size_t otherIndex = 0; otherIndex < retainedSiteCount; ++otherIndex) {
        if (execution.cancellationRequested()) {
          return cancelled();
        }
        if (otherIndex != siteIndex &&
            squaredDistance(proposed, retainedSites[otherIndex]) <
                minimumDistanceSquared) {
          preservesSpacing = false;
          break;
        }
      }
      if (preservesSpacing) {
        retainedSites[siteIndex] = proposed;
      }
    }

    std::vector<wp::Vector2> sites;
    sites.reserve(retainedSiteCount);
    for (auto const& site : retainedSites) {
      sites.push_back({fromGrid(site.x), fromGrid(site.y)});
    }
    auto const finalIteration = iteration + 1 == iterations;
    if (finalIteration &&
        execution.report(PrimitiveFieldLayoutPhase::LloydRelaxation, 0.65f)) {
      return cancelled();
    }
    auto rebuilt = buildBoundedLayout(layout.worldExtents, std::move(sites),
                                      &execution, finalIteration);
    if (rebuilt.cancelled()) {
      return rebuilt;
    }
    if (!rebuilt.succeeded()) {
      return failure("Lloyd relaxation iteration " +
                     std::to_string(iteration + 1) + " failed: " +
                     rebuilt.error);
    }
    if (rebuilt.layout->sites.size() != retainedSiteCount ||
        rebuilt.layout->cells.size() != retainedSiteCount) {
      return failure("Lloyd relaxation changed the retained site count.");
    }
    layout = std::move(*rebuilt.layout);
  }

  return {.layout = std::move(layout), .error = {}, .wasCancelled = false};
}

PrimitiveFieldLayoutResult validateFinalLayout(
    PrimitiveFieldLayout layout,
    IntPoint const& domainMinimum,
    IntPoint const& domainMaximum,
    int64_t spacing,
    ExecutionContext& execution) {
  auto const minimumDistanceSquared = spacing * spacing;
  for (size_t i = 0; i < layout.sites.size(); ++i) {
    if (execution.report(
            PrimitiveFieldLayoutPhase::Validation,
            0.98f + 0.019f * static_cast<float>(i + 1) /
                        static_cast<float>(layout.sites.size()))) {
      return cancelled();
    }
    IntPoint site;
    if (!convertToGrid(layout.sites[i].x, site.x) ||
        !convertToGrid(layout.sites[i].y, site.y) ||
        site.x < domainMinimum.x || site.x > domainMaximum.x ||
        site.y < domainMinimum.y || site.y > domainMaximum.y) {
      return failure(
          "Layout validation found a site outside the spacing-inset domain.");
    }
    for (size_t j = i + 1; j < layout.sites.size(); ++j) {
      if (execution.cancellationRequested()) {
        return cancelled();
      }
      IntPoint other;
      if (!convertToGrid(layout.sites[j].x, other.x) ||
          !convertToGrid(layout.sites[j].y, other.y) ||
          squaredDistance(site, other) < minimumDistanceSquared) {
        return failure(
            "Layout validation found sites below the requested minimum spacing.");
      }
    }
  }
  return {.layout = std::move(layout), .error = {}, .wasCancelled = false};
}

}  // namespace

PrimitiveFieldLayoutResult generatePrimitiveFieldLayout(
    PrimitiveFieldLayoutRequest const& request,
    PrimitiveFieldLayoutExecution const& execution) {
  ExecutionContext executionContext(&execution);
  try {
    if (executionContext.report(PrimitiveFieldLayoutPhase::Sampling, 0.0f)) {
      return cancelled();
    }
    IntPoint worldMinimum;
    IntPoint worldMaximum;
    std::string extentError;
    if (!validExtents(request.worldExtents, worldMinimum, worldMaximum,
                      extentError)) {
      return failure(std::move(extentError));
    }
    if (!std::isfinite(request.minimumSpacing)) {
      return failure("Minimum site spacing must be finite.");
    }
    if (request.minimumSpacing < 8.0f) {
      return failure("Minimum site spacing must be at least 8 world units.");
    }
    if (request.maximumSites == 0) {
      return failure("Maximum site count must be at least one.");
    }
    if (request.maximumSites > BW_WORLD_PRIMITIVE_COUNT_MAX) {
      return failure("Maximum site count exceeds the engine primitive limit.");
    }
    if (request.lloydIterations < 0 || request.lloydIterations > 20) {
      return failure("Lloyd iterations must be between 0 and 20.");
    }

    int64_t spacing = 0;
    auto scaledSpacing = static_cast<double>(request.minimumSpacing) *
                         static_cast<double>(CoordinateScale);
    if (scaledSpacing > static_cast<double>(MaximumExtentSpanUnits)) {
      return failure("Minimum site spacing is outside the supported range.");
    }
    spacing = static_cast<int64_t>(std::ceil(scaledSpacing));
    auto worldWidth = static_cast<double>(request.worldExtents.maximum.x) -
                      request.worldExtents.minimum.x;
    auto worldHeight = static_cast<double>(request.worldExtents.maximum.y) -
                       request.worldExtents.minimum.y;
    if (request.minimumSpacing > worldWidth ||
        request.minimumSpacing > worldHeight) {
      return failure("The world is too small to inset by half the requested spacing.");
    }

    auto halfSpacing = static_cast<double>(request.minimumSpacing) * 0.5;
    auto domainMinimumX =
        (static_cast<double>(request.worldExtents.minimum.x) + halfSpacing) *
        CoordinateScale;
    auto domainMinimumY =
        (static_cast<double>(request.worldExtents.minimum.y) + halfSpacing) *
        CoordinateScale;
    auto domainMaximumX =
        (static_cast<double>(request.worldExtents.maximum.x) - halfSpacing) *
        CoordinateScale;
    auto domainMaximumY =
        (static_cast<double>(request.worldExtents.maximum.y) - halfSpacing) *
        CoordinateScale;
    IntPoint domainMinimum{static_cast<int64_t>(std::ceil(domainMinimumX)),
                           static_cast<int64_t>(std::ceil(domainMinimumY))};
    IntPoint domainMaximum{static_cast<int64_t>(std::floor(domainMaximumX)),
                           static_cast<int64_t>(std::floor(domainMaximumY))};
    if (domainMinimum.x > domainMaximum.x ||
        domainMinimum.y > domainMaximum.y) {
      return failure("The requested spacing leaves no valid inset site domain.");
    }

    IntPoint centre{(worldMinimum.x + worldMaximum.x) / 2,
                    (worldMinimum.y + worldMaximum.y) / 2};
    centre.x = std::clamp(centre.x, domainMinimum.x, domainMaximum.x);
    centre.y = std::clamp(centre.y, domainMinimum.y, domainMaximum.y);

    std::vector<IntPoint> accepted;
    accepted.reserve(request.maximumSites);
    std::map<std::pair<int64_t, int64_t>, std::vector<size_t>> grid;
    auto addAccepted = [&](IntPoint const& point) {
      auto index = accepted.size();
      accepted.push_back(point);
      auto cellX = floorDivide(point.x - domainMinimum.x, spacing);
      auto cellY = floorDivide(point.y - domainMinimum.y, spacing);
      grid[{cellX, cellY}].push_back(index);
    };
    auto canAccept = [&](IntPoint const& point) {
      if (point.x < domainMinimum.x || point.x > domainMaximum.x ||
          point.y < domainMinimum.y || point.y > domainMaximum.y) {
        return false;
      }
      auto cellX = floorDivide(point.x - domainMinimum.x, spacing);
      auto cellY = floorDivide(point.y - domainMinimum.y, spacing);
      auto minimumDistanceSquared = spacing * spacing;
      for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
          auto found = grid.find({cellX + x, cellY + y});
          if (found == grid.end()) {
            continue;
          }
          for (auto index : found->second) {
            if (squaredDistance(point, accepted[index]) < minimumDistanceSquared) {
              return false;
            }
          }
        }
      }
      return true;
    };

    Pcg32 random(request.seed);
    std::priority_queue<Candidate, std::vector<Candidate>, CandidateFartherFirst>
        candidates;
    uint64_t sequence = 0;
    auto queueChildren = [&](IntPoint const& parent) {
      auto parentDistance = squaredDistance(parent, centre);
      int produced = 0;
      while (produced < CandidateCountPerSite) {
        auto rawX = static_cast<int32_t>(random.next() & 0xffffu) - 32768;
        auto rawY = static_cast<int32_t>(random.next() & 0xffffu) - 32768;
        auto rawDistance = static_cast<int64_t>(rawX) * rawX +
                           static_cast<int64_t>(rawY) * rawY;
        constexpr int64_t innerRadiusSquared = int64_t{16384} * 16384;
        constexpr int64_t outerRadiusSquared = int64_t{32768} * 32768;
        if (rawDistance < innerRadiusSquared ||
            rawDistance >= outerRadiusSquared) {
          continue;
        }
        IntPoint point{
            parent.x + roundedDivide(static_cast<int64_t>(rawX) * 2 * spacing,
                                     32768),
            parent.y + roundedDivide(static_cast<int64_t>(rawY) * 2 * spacing,
                                     32768)};
        auto distance = squaredDistance(point, centre);
        if (distance >= parentDistance) {
          candidates.push({point, distance, sequence++});
        }
        ++produced;
      }
    };

    addAccepted(centre);
    queueChildren(centre);
    while (accepted.size() < request.maximumSites && !candidates.empty()) {
      if (executionContext.report(
              PrimitiveFieldLayoutPhase::Sampling,
              0.25f * static_cast<float>(accepted.size()) /
                  static_cast<float>(request.maximumSites))) {
        return cancelled();
      }
      auto candidate = candidates.top();
      candidates.pop();
      if (!canAccept(candidate.point)) {
        continue;
      }
      addAccepted(candidate.point);
      queueChildren(candidate.point);
    }

    if (executionContext.report(PrimitiveFieldLayoutPhase::Sampling, 0.25f)) {
      return cancelled();
    }

    std::vector<wp::Vector2> sites;
    sites.reserve(accepted.size());
    for (auto const& point : accepted) {
      sites.push_back({fromGrid(point.x), fromGrid(point.y)});
    }
    if (request.lloydIterations > 0 &&
        executionContext.report(PrimitiveFieldLayoutPhase::LloydRelaxation,
                                0.25f)) {
      return cancelled();
    }
    auto initial = buildBoundedLayout(
        request.worldExtents, std::move(sites), &executionContext,
        request.lloydIterations == 0);
    if (initial.cancelled() || !initial.succeeded()) {
      return initial;
    }

    PrimitiveFieldLayoutResult generated = std::move(initial);
    if (request.lloydIterations > 0) {
      generated = relaxLayout(
          std::move(*generated.layout), worldMinimum, domainMinimum,
          domainMaximum, spacing, request.lloydIterations, executionContext);
      if (generated.cancelled() || !generated.succeeded()) {
        return generated;
      }
    }

    auto validated = validateFinalLayout(
        std::move(*generated.layout), domainMinimum, domainMaximum, spacing,
        executionContext);
    if (validated.cancelled() || !validated.succeeded()) {
      return validated;
    }
    executionContext.report(PrimitiveFieldLayoutPhase::Complete, 1.0f);
    return validated;
  } catch (std::exception const& error) {
    return failure(std::string("Primitive-field layout generation failed: ") +
                   error.what());
  }
}

PrimitiveFieldLayoutResult generatePrimitiveFieldLayout(
    PrimitiveFieldLayoutRequest const& request) {
  return generatePrimitiveFieldLayout(request, {});
}

PrimitiveFieldLayoutResult buildBoundedPrimitiveFieldLayout(
    PrimitiveFieldExtents const& worldExtents,
    std::vector<wp::Vector2> sites) {
  try {
    return buildBoundedLayout(worldExtents, std::move(sites));
  } catch (std::exception const& error) {
    return failure(std::string("Bounded Voronoi construction failed: ") +
                   error.what());
  }
}

}  // namespace bw::core
