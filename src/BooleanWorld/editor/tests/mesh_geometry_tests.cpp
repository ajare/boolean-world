#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "MeshGeometry.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void ringQueriesUseAuthoredMeshGeometry() {
  wp::geometry::Mesh mesh;
  auto outer = editor::meshGeometry::addDrawnRing(
      mesh, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
  auto inner = editor::meshGeometry::addDrawnRing(
      mesh, {{2, 2}, {4, 2}, {4, 4}, {2, 4}});

  require(editor::meshGeometry::pointInsideRing(
              mesh, mesh.getPolygon(outer), {5, 5}),
          "pointInsideRing rejected an interior point");
  require(!editor::meshGeometry::pointInsideRing(
              mesh, mesh.getPolygon(outer), {15, 5}),
          "pointInsideRing accepted an exterior point");
  require(std::abs(editor::meshGeometry::ringArea(mesh, outer) - 200.0f) <
              0.001f,
          "ringArea did not return twice the authored area");
  require(editor::meshGeometry::innermostRingAt(mesh, {}, {3, 3}) == inner,
          "innermostRingAt did not choose the smallest containing Ring");
}

void segmentQueriesDistinguishCrossingsFromTouches() {
  using namespace editor::meshGeometry;
  require(segmentsIntersect({0, 0}, {2, 2}, {0, 2}, {2, 0}),
          "crossing segments did not intersect");
  require(segmentsIntersect({0, 0}, {1, 0}, {1, 0}, {2, 0}),
          "touching segments did not intersect");
  require(!properSegmentsIntersect({0, 0}, {1, 0}, {1, 0}, {2, 0}),
          "a shared endpoint was reported as a proper crossing");
}
}  // namespace

int main() {
  try {
    ringQueriesUseAuthoredMeshGeometry();
    segmentQueriesDistinguishCrossingsFromTouches();
    std::cout << "Mesh geometry queries passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
