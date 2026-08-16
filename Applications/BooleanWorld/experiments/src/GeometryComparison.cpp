#include "GeometryComparison.h"

#include <cmath>
#include <random>
#include <stdexcept>

#include <core/Arrangement.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/World.h>

namespace bw::experiments {
namespace {
using bw::core::arr::ArrangementResult;

GeometryPredicate QueryPublishedSnapshot(
    bw::core::WorldData const& worldData,
    wp::Vector2 const& position) {
  auto faceIndex = worldData.getContainingFaceIndex(position);
  if (faceIndex == ~0u) {
    return {};
  }
  auto const& face = worldData.getArrangement().faces[faceIndex];
  return {face.solid, face.solid ? face.primitiveIndex : ~0u};
}

GeometryPredicate QueryNewEngine(
    ArrangementResult const& arrangement,
    wp::Vector2 const& position) {
  bw::core::arr::FixedPointVertex point{
      bw::core::arr::ToFixedPointCoordinate(position.x),
      bw::core::arr::ToFixedPointCoordinate(position.y)};
  for (auto const& face : arrangement.faces) {
    if (!bw::core::arr::PointInFace(point, face, arrangement)) {
      continue;
    }
    return {
        face.solid,
        face.solid ? face.primitiveIndex : ~0u};
  }
  return {};
}

double BoundaryTwiceArea(
    std::vector<uint32_t> const& boundary,
    uint32_t faceIndex,
    ArrangementResult const& arrangement) {
  double area = 0;
  for (auto edgeIndex : boundary) {
    auto const& edge = arrangement.edges[edgeIndex];
    auto forward = edge.face[0] == faceIndex;
    auto const& a = arrangement.vertices[edge.v[forward ? 0 : 1]];
    auto const& b = arrangement.vertices[edge.v[forward ? 1 : 0]];
    area += double(a.x) * double(b.y) - double(b.x) * double(a.y);
  }
  return area;
}

double NewSolidArea(ArrangementResult const& arrangement) {
  double twiceArea = 0;
  for (uint32_t faceIndex = 0;
       faceIndex < uint32_t(arrangement.faces.size()); ++faceIndex) {
    auto const& face = arrangement.faces[faceIndex];
    if (!face.solid) {
      continue;
    }
    twiceArea += std::abs(BoundaryTwiceArea(
        face.outerBoundary, faceIndex, arrangement));
    for (auto const& hole : face.innerBoundaries) {
      twiceArea -= std::abs(BoundaryTwiceArea(
          hole, faceIndex, arrangement));
    }
  }
  constexpr auto scale =
      double(bw::core::arr::FixedPointUnitsPerWorldUnit);
  return twiceArea * 0.5 / (scale * scale);
}

bool InBounds(wp::Vector2 const& point, wp::BoundingBox const& bounds) {
  auto const& min = bounds.getMinExtent();
  auto const& max = bounds.getMaxExtent();
  return point.x >= min.x && point.x <= max.x &&
         point.y >= min.y && point.y <= max.y;
}

bool OnArrangementBoundary(
    wp::Vector2 const& point,
    ArrangementResult const& arrangement) {
  constexpr double tolerance =
      0.25 / bw::core::arr::FixedPointUnitsPerWorldUnit;
  constexpr auto scale =
      double(bw::core::arr::FixedPointUnitsPerWorldUnit);
  for (auto const& edge : arrangement.edges) {
    auto const& fixedA = arrangement.vertices[edge.v[0]];
    auto const& fixedB = arrangement.vertices[edge.v[1]];
    double ax = double(fixedA.x) / scale;
    double ay = double(fixedA.y) / scale;
    double bx = double(fixedB.x) / scale;
    double by = double(fixedB.y) / scale;
    double dx = bx - ax;
    double dy = by - ay;
    double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0) {
      continue;
    }
    double t = ((point.x - ax) * dx + (point.y - ay) * dy) /
               lengthSquared;
    if (t < 0 || t > 1) {
      continue;
    }
    double cross = dx * (point.y - ay) - dy * (point.x - ax);
    if (std::abs(cross) / std::sqrt(lengthSquared) <= tolerance) {
      return true;
    }
  }
  return false;
}
}  // namespace

uint32_t GeometryComparisonReport::totalSampleCount() const {
  uint32_t total = 0;
  for (auto count : sampleCounts) {
    total += count;
  }
  return total;
}

bool GeometryComparisonReport::matches() const {
  return disagreements.empty();
}

GeometryComparisonReport CompareWorldGeometry(
    bw::core::World& world,
    GeometryComparisonOptions const& options) {
  auto generator = dynamic_cast<bw::core::DynamicWorldDataGenerator*>(
      world.getWorldDataGenerator());
  if (!generator) {
    throw std::invalid_argument(
        "geometry comparison requires DynamicWorldDataGenerator");
  }

  world.setAlwaysUpdateVertices(true);
  generator->setAlwaysUpdateVertices(true);
  generator->setAllowCommitIfVisible(true);
  generator->setLayerSelection(options.layerSelection);

  auto const& bounds = world.getExtents();
  auto centre = bounds.getCentre();
  world.update(
      0,
      {centre, 0, 1, 60, 256, false, false, 0},
      bounds.getSize());
  generator->generateBlocking();
  auto oldWorld = world.getWorldData(centre, 0);

  auto primitives = generator->getActiveClippingPrimitives();
  bw::core::ArrangementWorldDataGenerator arrangementGenerator;
  arrangementGenerator.generate(primitives);
  auto newWorld = arrangementGenerator.getWorldData();

  GeometryComparisonReport report;
  report.oldSolidArea = NewSolidArea(oldWorld->getArrangement());
  report.newSolidArea = NewSolidArea(*newWorld);

  auto sample = [&](wp::Vector2 const& position, SampleKind kind) {
    // Membership on the boundary itself is intentionally undefined. Samples
    // cluster near edges, but exact boundary hits are not comparisons.
    if (OnArrangementBoundary(position, *newWorld)) {
      return;
    }
    auto oldPredicate = QueryPublishedSnapshot(*oldWorld, position);
    auto newPredicate = QueryNewEngine(*newWorld, position);
    ++report.sampleCounts[std::size_t(kind)];
    if (oldPredicate != newPredicate) {
      report.disagreements.push_back(
          {position, kind, oldPredicate, newPredicate});
    }
  };

  if (options.gridResolution > 0) {
    auto const& min = bounds.getMinExtent();
    auto size = bounds.getSize();
    for (uint32_t y = 0; y < options.gridResolution; ++y) {
      for (uint32_t x = 0; x < options.gridResolution; ++x) {
        sample(
            {min.x + size.x * (float(x) + 0.5f) / options.gridResolution,
             min.y + size.y * (float(y) + 0.5f) / options.gridResolution},
            SampleKind::UniformGrid);
      }
    }
  }

  std::mt19937 random(options.randomSeed);
  std::uniform_real_distribution<float> randomX(
      bounds.getMinExtent().x, bounds.getMaxExtent().x);
  std::uniform_real_distribution<float> randomY(
      bounds.getMinExtent().y, bounds.getMaxExtent().y);
  for (uint32_t i = 0; i < options.randomSampleCount; ++i) {
    sample({randomX(random), randomY(random)}, SampleKind::Random);
  }

  auto sampleNearEdge = [&](wp::Vector2 const& a, wp::Vector2 const& b) {
    auto edge = b - a;
    auto length = edge.length();
    if (length == 0) {
      return;
    }
    wp::Vector2 normal{-edge.y / length, edge.x / length};
    for (uint32_t i = 0; i < options.edgeSamplesPerEdge; ++i) {
      auto t = float(i + 1) / float(options.edgeSamplesPerEdge + 1);
      auto onEdge = a + edge * t;
      for (auto side : {-1.0f, 1.0f}) {
        auto position = onEdge + normal * options.edgeOffset * side;
        if (InBounds(position, bounds)) {
          sample(position, SampleKind::NearEdge);
        }
      }
    }
  };

  // Sample both independently-built arrangement descriptions.
  for (auto const& arrangementEdge : newWorld->edges) {
    if (newWorld->faces[arrangementEdge.face[0]].solid ==
        newWorld->faces[arrangementEdge.face[1]].solid) {
      continue;
    }
    auto const& fixedA = newWorld->vertices[arrangementEdge.v[0]];
    auto const& fixedB = newWorld->vertices[arrangementEdge.v[1]];
    sampleNearEdge(
        {bw::core::arr::ToWorldCoordinate(fixedA.x),
         bw::core::arr::ToWorldCoordinate(fixedA.y)},
        {bw::core::arr::ToWorldCoordinate(fixedB.x),
         bw::core::arr::ToWorldCoordinate(fixedB.y)});
  }

  return report;
}
}  // namespace bw::experiments
