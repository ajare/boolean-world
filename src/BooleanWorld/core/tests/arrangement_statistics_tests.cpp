#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>
#include <willpower/wayfinder/Floor.h>
#include <willpower/wayfinder/Mesh.h>
#include <willpower/wayfinder/PathDatabase.h>
#include <willpower/wayfinder/Sector.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void consumesExistingTriangulation() {
  std::vector<wp::Vector2> vertices{
      {0.0f, 0.0f}, {4.0f, 0.0f}, {4.0f, 4.0f}, {0.0f, 4.0f},
      {1.0f, 1.0f}, {1.0f, 3.0f}, {3.0f, 3.0f}, {3.0f, 1.0f}};
  std::vector<wp::wayfinder::Triangle> triangles{
      {0, 1, 7}, {0, 7, 4}, {1, 2, 6}, {1, 6, 7},
      {2, 3, 5}, {2, 5, 6}, {3, 0, 4}, {3, 4, 5}};

  wp::wayfinder::Mesh mesh(vertices, triangles);
  require(mesh.getNumSectors() == 1,
          "direct Wayfinder triangulation split one connected sector");
  auto* sector = mesh.getSector(0);
  require(sector->getNumHoles() == 1,
          "direct Wayfinder triangulation did not recover its hole boundary");
  auto* floor = sector->getFloor(0);
  require(floor->getNumPolygons() == triangles.size(),
          "direct Wayfinder construction retriangulated its input");

  floor->calculatePaths(0);
  auto path = floor->getPath({2.0f, 3.5f}, 4, 0);
  size_t portalCount = 0;
  for ([[maybe_unused]] auto portal : path) {
    ++portalCount;
  }
  require(portalCount > 0 && portalCount < triangles.size(),
          "target-tree route did not reconstruct a portal path");

  wp::wayfinder::PathDatabase boundedDatabase;
  boundedDatabase.setSize(static_cast<uint32_t>(triangles.size()));
  boundedDatabase.setMaxCachedTargets(2);
  auto const* polygonisation = floor->getPolygonisation();
  polygonisation->calculatePaths(0, &boundedDatabase);
  polygonisation->calculatePaths(1, &boundedDatabase);
  polygonisation->calculatePaths(2, &boundedDatabase);
  require(boundedDatabase.getNumCachedTargets() == 2,
          "PathDatabase exceeded its target-tree cache limit");
  require(!boundedDatabase.isCalculated(0),
          "PathDatabase did not evict its least recently used target tree");

  polygonisation->calculatePaths(0, &boundedDatabase);
  require(!boundedDatabase.getPath(4, 0, *polygonisation).empty(),
          "compact target tree did not reconstruct a portal path");

  wp::wayfinder::PathDatabase largeDatabase;
  largeDatabase.setSize(10'000);
  require(largeDatabase.getNumCachedTargets() == 0,
          "PathDatabase eagerly allocated a target tree");
}

void reportsArrangementDiagnostics() {
  using namespace bw::core;
  namespace arr = bw::core::arr;

  arr::ArrangementPrimitive primitive{
      {{{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}},
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      0,
      7,
      {}};
  ArrangementStats stats;
  auto arrangement = arr::BuildArrangement({primitive}, &stats);
  ArrangementWorldData worldData(
      arrangement,
      wp::BoundingBox(0.0f, 0.0f, 1.0f, 1.0f),
      1.0f,
      &stats,
      {},
      true);

  require(stats.vertexCount == 4,
          "arrangement diagnostics did not report fixed-point vertices");
  require(stats.edgeCount == 4,
          "arrangement diagnostics did not report arrangement edges");
  require(stats.faceCount == 2,
          "arrangement diagnostics did not report exterior and solid faces");
  require(stats.triangleCount == 2,
          "arrangement diagnostics did not report solid-face triangles");
  require(stats.wallCount == 4,
          "arrangement diagnostics did not report border walls");
  require(stats.buildPSLGTimeNs + stats.classificationTimeNs > 0,
          "arrangement diagnostics did not record construction timings");
  require(worldData.getWayfinderMesh() != nullptr,
          "requested Wayfinder mesh was not returned in world data");
  require(worldData.getWayfinderMesh()->getNumSectors() == 1,
          "Wayfinder did not preserve the arrangement's connected sector");
  require(worldData.getWayfinderMesh()->getSector(0)->getFloor(0)->getNumPolygons() ==
              worldData.getTriangles().size(),
          "Wayfinder did not consume the arrangement triangulation directly");
  require(stats.wayfinderMeshTimeNs > 0,
          "arrangement diagnostics did not time Wayfinder mesh generation");

  ArrangementWorldData withoutWayfinder(
      arrangement,
      wp::BoundingBox(0.0f, 0.0f, 1.0f, 1.0f),
      1.0f);
  require(withoutWayfinder.getWayfinderMesh() == nullptr,
          "Wayfinder mesh generation was not optional");
}

}  // namespace

int main() {
  try {
    consumesExistingTriangulation();
    reportsArrangementDiagnostics();
    std::cout << "Arrangement diagnostics report generated topology\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
