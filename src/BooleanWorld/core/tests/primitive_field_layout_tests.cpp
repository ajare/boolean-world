#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Defines.h>
#include <core/PrimitiveFieldLayout.h>

namespace {
using bw::core::PrimitiveFieldExtents;
using bw::core::PrimitiveFieldLayout;
using bw::core::PrimitiveFieldLayoutRequest;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

double area(std::vector<wp::Vector2> const& vertices) {
  double twiceArea = 0.0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& current = vertices[i];
    auto const& next = vertices[(i + 1) % vertices.size()];
    twiceArea += static_cast<double>(current.x) * next.y -
                 static_cast<double>(current.y) * next.x;
  }
  return twiceArea * 0.5;
}

wp::Vector2 centroid(std::vector<wp::Vector2> const& vertices) {
  double twiceArea = 0.0;
  double xMoment = 0.0;
  double yMoment = 0.0;
  for (size_t i = 0; i < vertices.size(); ++i) {
    auto const& current = vertices[i];
    auto const& next = vertices[(i + 1) % vertices.size()];
    auto cross = static_cast<double>(current.x) * next.y -
                 static_cast<double>(next.x) * current.y;
    twiceArea += cross;
    xMoment += (static_cast<double>(current.x) + next.x) * cross;
    yMoment += (static_cast<double>(current.y) + next.y) * cross;
  }
  return {static_cast<float>(xMoment / (3.0 * twiceArea)),
          static_cast<float>(yMoment / (3.0 * twiceArea))};
}

double centroidEnergy(PrimitiveFieldLayout const& layout) {
  double result = 0.0;
  for (size_t i = 0; i < layout.sites.size(); ++i) {
    auto cellCentroid = centroid(layout.cells[i].vertices);
    auto dx = static_cast<double>(layout.sites[i].x) - cellCentroid.x;
    auto dy = static_cast<double>(layout.sites[i].y) - cellCentroid.y;
    result += dx * dx + dy * dy;
  }
  return result;
}

bool exactPoint(wp::Vector2 const& lhs, wp::Vector2 const& rhs) {
  return std::bit_cast<uint32_t>(lhs.x) == std::bit_cast<uint32_t>(rhs.x) &&
         std::bit_cast<uint32_t>(lhs.y) == std::bit_cast<uint32_t>(rhs.y);
}

uint64_t layoutFingerprint(PrimitiveFieldLayout const& layout) {
  uint64_t hash = 14695981039346656037ULL;
  auto mix = [&](uint32_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  auto mixPoint = [&](wp::Vector2 const& point) {
    mix(std::bit_cast<uint32_t>(point.x));
    mix(std::bit_cast<uint32_t>(point.y));
  };
  mixPoint(layout.worldExtents.minimum);
  mixPoint(layout.worldExtents.maximum);
  mix(static_cast<uint32_t>(layout.sites.size()));
  for (size_t i = 0; i < layout.sites.size(); ++i) {
    mixPoint(layout.sites[i]);
    mix(static_cast<uint32_t>(layout.cells[i].vertices.size()));
    for (auto const& vertex : layout.cells[i].vertices) {
      mixPoint(vertex);
    }
  }
  return hash;
}

void requireExactLayout(PrimitiveFieldLayout const& lhs,
                        PrimitiveFieldLayout const& rhs) {
  require(exactPoint(lhs.worldExtents.minimum, rhs.worldExtents.minimum) &&
              exactPoint(lhs.worldExtents.maximum, rhs.worldExtents.maximum),
          "deterministic layouts changed their extents");
  require(lhs.sites.size() == rhs.sites.size(),
          "deterministic layouts changed site count");
  require(lhs.cells.size() == rhs.cells.size(),
          "deterministic layouts changed cell count");
  for (size_t i = 0; i < lhs.sites.size(); ++i) {
    require(exactPoint(lhs.sites[i], rhs.sites[i]),
            "deterministic layouts changed site ordering or coordinates");
    require(lhs.cells[i].vertices.size() == rhs.cells[i].vertices.size(),
            "deterministic layouts changed cell shape");
    for (size_t j = 0; j < lhs.cells[i].vertices.size(); ++j) {
      require(exactPoint(lhs.cells[i].vertices[j], rhs.cells[i].vertices[j]),
              "deterministic layouts changed cell vertex ordering or coordinates");
    }
  }
}

PrimitiveFieldLayout generate(PrimitiveFieldLayoutRequest request) {
  auto result = bw::core::generatePrimitiveFieldLayout(request);
  require(result.succeeded(), "layout generation failed: " + result.error);
  return std::move(*result.layout);
}

void requireLayoutInvariants(PrimitiveFieldLayout const& layout,
                             float spacing) {
  require(!layout.sites.empty(), "layout contains no sites");
  require(layout.cells.size() == layout.sites.size(),
          "not every site owns exactly one cell");

  auto inset = spacing * 0.5f;
  require(std::any_of(
              layout.sites.begin(), layout.sites.end(),
              [](wp::Vector2 const& site) {
                return exactPoint(site, wp::Vector2::ZERO);
              }),
          "layout does not retain its required origin site");
  auto centre = wp::Vector2{
      (layout.worldExtents.minimum.x + layout.worldExtents.maximum.x) * 0.5f,
      (layout.worldExtents.minimum.y + layout.worldExtents.maximum.y) * 0.5f};
  double previousCentreDistance = -1.0;
  for (auto const& site : layout.sites) {
    auto centreDx = static_cast<double>(site.x) - centre.x;
    auto centreDy = static_cast<double>(site.y) - centre.y;
    auto centreDistance = centreDx * centreDx + centreDy * centreDy;
    require(centreDistance >= previousCentreDistance,
            "site output does not proceed center-outward");
    previousCentreDistance = centreDistance;
    require(site.x >= layout.worldExtents.minimum.x + inset &&
                site.x <= layout.worldExtents.maximum.x - inset &&
                site.y >= layout.worldExtents.minimum.y + inset &&
                site.y <= layout.worldExtents.maximum.y - inset,
            "site lies outside the spacing-inset domain");
  }
  for (size_t i = 0; i < layout.sites.size(); ++i) {
    for (size_t j = i + 1; j < layout.sites.size(); ++j) {
      auto dx = static_cast<double>(layout.sites[i].x) - layout.sites[j].x;
      auto dy = static_cast<double>(layout.sites[i].y) - layout.sites[j].y;
      require(std::sqrt(dx * dx + dy * dy) +
                      bw::core::PrimitiveFieldNumericTolerance >=
                  spacing,
              "site pair violates minimum spacing");
    }
  }

  bool touchesLeft = false;
  bool touchesRight = false;
  bool touchesBottom = false;
  bool touchesTop = false;
  double totalArea = 0.0;
  for (auto const& cell : layout.cells) {
    require(cell.vertices.size() >= 3, "cell has fewer than three vertices");
    auto cellArea = area(cell.vertices);
    require(cellArea > 0.0, "cell is degenerate or not counter-clockwise");
    totalArea += cellArea;
    for (auto const& vertex : cell.vertices) {
      require(vertex.x >= layout.worldExtents.minimum.x &&
                  vertex.x <= layout.worldExtents.maximum.x &&
                  vertex.y >= layout.worldExtents.minimum.y &&
                  vertex.y <= layout.worldExtents.maximum.y,
              "cell vertex lies outside world extents");
      touchesLeft |= vertex.x == layout.worldExtents.minimum.x;
      touchesRight |= vertex.x == layout.worldExtents.maximum.x;
      touchesBottom |= vertex.y == layout.worldExtents.minimum.y;
      touchesTop |= vertex.y == layout.worldExtents.maximum.y;
    }
  }
  require(touchesLeft && touchesRight && touchesBottom && touchesTop,
          "bounded cells do not reach every world boundary");
  auto width = static_cast<double>(layout.worldExtents.maximum.x) -
               layout.worldExtents.minimum.x;
  auto height = static_cast<double>(layout.worldExtents.maximum.y) -
                layout.worldExtents.minimum.y;
  require(std::abs(totalArea - width * height) <= width * height * 1.0e-4,
          "cells do not collectively cover the world extents");
}

void generatesSquareAndNonsquareBoundedLayouts() {
  auto square = generate({{{-128.0f, -128.0f}, {128.0f, 128.0f}},
                          64.0f,
                          BW_WORLD_PRIMITIVE_COUNT_MAX,
                          17,
                          0});
  require(square.sites.size() > 1 &&
              square.sites.size() < BW_WORLD_PRIMITIVE_COUNT_MAX,
          "uncapped square fixture did not naturally terminate");
  require(exactPoint(square.sites.front(), {0.0f, 0.0f}),
          "square sampling did not begin at the world centre");
  requireLayoutInvariants(square, 64.0f);

  auto nonsquare = generate({{{-150.0f, -80.0f}, {170.0f, 80.0f}},
                             40.0f,
                             BW_WORLD_PRIMITIVE_COUNT_MAX,
                             -29,
                             0});
  require(nonsquare.sites.size() > 1 &&
              nonsquare.sites.size() < BW_WORLD_PRIMITIVE_COUNT_MAX,
          "uncapped nonsquare fixture did not naturally terminate");
  require(std::any_of(
              nonsquare.sites.begin(), nonsquare.sites.end(),
              [](wp::Vector2 const& site) {
                return exactPoint(site, wp::Vector2::ZERO);
              }),
          "nonsquare sampling did not retain the origin");
  requireLayoutInvariants(nonsquare, 40.0f);
}

void cappedLayoutStopsAtMaximumAndIsAnUncappedPrefix() {
  PrimitiveFieldLayoutRequest request{
      {{-256.0f, -192.0f}, {256.0f, 192.0f}}, 48.0f, 7, 123456, 0};
  auto capped = generate(request);
  require(capped.sites.size() == 7,
          "capped layout did not stop at the requested maximum");

  request.maximumSites = BW_WORLD_PRIMITIVE_COUNT_MAX;
  auto uncapped = generate(request);
  require(uncapped.sites.size() > capped.sites.size(),
          "uncapped fixture did not produce more sites than capped fixture");
  for (size_t i = 0; i < capped.sites.size(); ++i) {
    require(exactPoint(capped.sites[i], uncapped.sites[i]),
            "capped sampling is not the prefix of center-outward sampling");
  }
  requireLayoutInvariants(capped, 48.0f);
}

void generatesTheDefaultEditorBatchWithoutOvergeneration() {
  auto layout = generate(
      {{{-4096.0f, -4096.0f}, {4096.0f, 4096.0f}}, 128.0f, 2000, 0, 5});
  require(layout.sites.size() == 2000 && layout.cells.size() == 2000,
          "the default editor request did not stop at exactly 2000 sites");
}

void repeatsExactSiteAndCellOutput() {
  PrimitiveFieldLayoutRequest request{
      {{-96.0f, -64.0f}, {160.0f, 128.0f}}, 56.0f, 6, -7654321, 0};
  auto first = generate(request);
  auto second = generate(request);
  require(first.sites.size() == 6, "deterministic fixture was not capped");
  require(layoutFingerprint(first) == 0x8ff4dc4a3afc3691ULL,
          "the fixed deterministic site-and-cell fixture changed");
  requireExactLayout(first, second);
}

void respectsInsetForNongridWorldExtents() {
  auto layout = generate(
      {{{-64.00012f, -48.00012f}, {63.99988f, 47.99988f}},
       16.0f,
       12,
       91,
       0});
  requireLayoutInvariants(layout, 16.0f);
}

void acceptsAOneSiteInsetDomain() {
  auto layout = generate(
      {{{-32.0f, -32.0f}, {32.0f, 32.0f}}, 64.0f, 50, 8, 0});
  require(layout.sites.size() == 1,
          "a point-sized inset domain should retain its centre site");
  require(layout.cells.size() == 1 && layout.cells[0].vertices.size() == 4,
          "one site should own the complete rectangular world cell");
  requireLayoutInvariants(layout, 64.0f);
}

void relaxesDeterministicallyAcrossSupportedIterationCounts() {
  PrimitiveFieldLayoutRequest defaults;
  require(defaults.lloydIterations == 5,
          "the core request did not default to five Lloyd iterations");

  PrimitiveFieldLayoutRequest request{
      {{-180.0f, -90.0f}, {220.0f, 150.0f}}, 42.0f, 18, 314159, 0};
  std::vector<int32_t> iterationCounts{0, 1, 5, 20};
  std::vector<uint64_t> fingerprints;
  std::vector<PrimitiveFieldLayout> layouts;
  for (auto iterations : iterationCounts) {
    request.lloydIterations = iterations;
    auto first = generate(request);
    auto repeated = generate(request);
    requireExactLayout(first, repeated);
    require(first.sites.size() == 18,
            "Lloyd relaxation changed the capped site count");
    requireLayoutInvariants(first, request.minimumSpacing);
    fingerprints.push_back(layoutFingerprint(first));
    layouts.push_back(std::move(first));
  }

  std::vector<uint64_t> const expectedFingerprints{
      0x6904315593ceaa9dULL, 0x3dc415c5e1b6f35dULL,
      0x9e494881d5530c27ULL, 0x8b0223cc0a157603ULL};
  require(fingerprints == expectedFingerprints,
          "a zero/one/default/maximum-iteration fixture changed");
  require(centroidEnergy(layouts[1]) < centroidEnergy(layouts[0]),
          "one Lloyd iteration did not regularize the representative layout");
  require(centroidEnergy(layouts[2]) < centroidEnergy(layouts[0]),
          "the default Lloyd iterations did not regularize the layout");

  size_t retainedSites = 0;
  bool rejectedOutsideInset = false;
  auto inset = request.minimumSpacing * 0.5f;
  for (size_t i = 0; i < layouts[0].sites.size(); ++i) {
    auto const& initialSite = layouts[0].sites[i];
    bool retained = false;
    for (auto const& relaxedSite : layouts[1].sites) {
      retained |= exactPoint(initialSite, relaxedSite);
    }
    retainedSites += retained ? 1u : 0u;
    if (!retained) {
      continue;
    }

    auto proposed = centroid(layouts[0].cells[i].vertices);
    rejectedOutsideInset |=
        proposed.x < request.worldExtents.minimum.x + inset ||
        proposed.x > request.worldExtents.maximum.x - inset ||
        proposed.y < request.worldExtents.minimum.y + inset ||
        proposed.y > request.worldExtents.maximum.y - inset;
  }
  require(retainedSites < layouts[0].sites.size(),
          "one Lloyd iteration did not move any eligible site");
  require(rejectedOutsideInset,
          "the boundary fixture did not reject an out-of-inset centroid");
  for (auto const& layout : layouts) {
    require(std::any_of(
                layout.sites.begin(), layout.sites.end(),
                [](wp::Vector2 const& site) {
                  return exactPoint(site, wp::Vector2::ZERO);
                }),
            "Lloyd relaxation moved or removed the pinned origin site");
  }
}

void reportsMonotonicProgressThroughPublicPhases() {
  std::vector<bw::core::PrimitiveFieldLayoutProgress> progress;
  auto result = bw::core::generatePrimitiveFieldLayout(
      {{{-256.0f, -192.0f}, {256.0f, 192.0f}}, 32.0f, 80, 42, 2},
      {{}, [&](auto const& update) { progress.push_back(update); }});
  require(result.succeeded(), "progress fixture did not complete");
  require(!progress.empty(), "layout generation reported no progress");

  float previous = 0.0f;
  bool sawSampling = false;
  bool sawLloyd = false;
  bool sawVoronoi = false;
  bool sawValidation = false;
  for (auto const& update : progress) {
    require(update.completion >= 0.0f && update.completion <= 1.0f,
            "layout progress left its documented bounds");
    require(update.completion >= previous,
            "layout progress moved backwards");
    previous = update.completion;
    sawSampling |= update.phase ==
                   bw::core::PrimitiveFieldLayoutPhase::Sampling;
    sawLloyd |= update.phase ==
                bw::core::PrimitiveFieldLayoutPhase::LloydRelaxation;
    sawVoronoi |= update.phase ==
                  bw::core::PrimitiveFieldLayoutPhase::VoronoiConstruction;
    sawValidation |= update.phase ==
                     bw::core::PrimitiveFieldLayoutPhase::Validation;
  }
  require(sawSampling && sawLloyd && sawVoronoi && sawValidation,
          "progress omitted a meaningful public pipeline phase");
  require(progress.back().phase ==
                  bw::core::PrimitiveFieldLayoutPhase::Complete &&
              progress.back().completion == 1.0f,
          "progress did not end with the public completion signal");
}

void cancelsWithinEveryExpensiveCorePhase() {
  using Phase = bw::core::PrimitiveFieldLayoutPhase;
  struct Fixture {
    Phase phase;
    float threshold;
    char const* name;
  };
  std::vector<Fixture> fixtures{
      {Phase::Sampling, 0.01f, "sampling"},
      {Phase::LloydRelaxation, 0.26f, "Lloyd relaxation"},
      {Phase::VoronoiConstruction, 0.70f, "Voronoi construction"},
      {Phase::Validation, 0.90f, "validation"}};

  for (auto const& fixture : fixtures) {
    std::stop_source stopSource;
    bool reachedPhase = false;
    auto started = std::chrono::steady_clock::now();
    auto result = bw::core::generatePrimitiveFieldLayout(
        {{{-4096.0f, -4096.0f}, {4096.0f, 4096.0f}},
         128.0f,
         2000,
         17,
         5},
        {stopSource.get_token(), [&](auto const& update) {
           if (update.phase == fixture.phase &&
               update.completion >= fixture.threshold) {
             reachedPhase = true;
             stopSource.request_stop();
           }
         }});
    auto elapsed = std::chrono::steady_clock::now() - started;
    require(reachedPhase,
            std::string("cancellation fixture did not reach ") + fixture.name);
    require(result.cancelled() && !result.succeeded() && result.error.empty(),
            std::string("cancellation was not observed during ") + fixture.name);
    require(elapsed < std::chrono::seconds(5),
            std::string("cancellation latency was unbounded during ") +
                fixture.name);
  }
}

void rejectsInvalidAndDegenerateInputs() {
  auto valid = PrimitiveFieldLayoutRequest{
      {{-64.0f, -64.0f}, {64.0f, 64.0f}}, 16.0f, 10, 0, 0};

  auto request = valid;
  request.minimumSpacing = std::numeric_limits<float>::infinity();
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "non-finite spacing was accepted");

  request = valid;
  request.worldExtents.maximum.x =
      std::numeric_limits<float>::quiet_NaN();
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "non-finite extents were accepted");

  request = valid;
  request.minimumSpacing = 7.0f;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "spacing below the supported minimum was accepted");

  request = valid;
  request.minimumSpacing = 129.0f;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "an impossible inset domain was accepted");

  request = valid;
  request.worldExtents = {{10.0f, -64.0f}, {138.0f, 64.0f}};
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "a spacing-inset domain excluding the origin was accepted");

  request = valid;
  request.maximumSites = 0;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "a zero maximum was accepted");

  request = valid;
  request.maximumSites = BW_WORLD_PRIMITIVE_COUNT_MAX + 1u;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "a maximum above the engine limit was accepted");

  request = valid;
  request.lloydIterations = -1;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "a negative Lloyd iteration count was accepted");

  request = valid;
  request.lloydIterations = 21;
  require(!bw::core::generatePrimitiveFieldLayout(request).succeeded(),
          "a Lloyd iteration count above twenty was accepted");

  auto extents = PrimitiveFieldExtents{{0.0f, 0.0f}, {100.0f, 100.0f}};
  auto duplicates = bw::core::buildBoundedPrimitiveFieldLayout(
      extents, {{25.0f, 25.0f}, {25.0f, 25.0f}});
  require(!duplicates.succeeded() && duplicates.error.find("Duplicate") != std::string::npos,
          "duplicate sites did not produce an actionable error");

  auto collapsed = bw::core::buildBoundedPrimitiveFieldLayout(
      extents, {{25.0f, 25.0f}, {25.00001f, 25.0f}});
  require(!collapsed.succeeded(),
          "sites degenerate on the public grid were accepted");

  auto outside = bw::core::buildBoundedPrimitiveFieldLayout(
      extents, {{25.0f, 25.0f}, {101.0f, 25.0f}});
  require(!outside.succeeded(), "an out-of-domain site was accepted");
}

}  // namespace

int main() {
  try {
    generatesSquareAndNonsquareBoundedLayouts();
    cappedLayoutStopsAtMaximumAndIsAnUncappedPrefix();
    generatesTheDefaultEditorBatchWithoutOvergeneration();
    repeatsExactSiteAndCellOutput();
    respectsInsetForNongridWorldExtents();
    acceptsAOneSiteInsetDomain();
    relaxesDeterministicallyAcrossSupportedIterationCounts();
    reportsMonotonicProgressThroughPublicPhases();
    cancelsWithinEveryExpensiveCorePhase();
    rejectsInvalidAndDegenerateInputs();
    std::cout << "Deterministic bounded primitive-field layout tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
