#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::arr::Cycle;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::PSLG;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Cycle makeCycle(std::vector<int> vertices, int64_t area) {
  Cycle cycle;
  cycle.vis = std::move(vertices);
  cycle.area = area;
  return cycle;
}

void nestsAFlushVoidInsideItsRoom() {
  // The void shares the room's left bounding-box bound. Its sample remains
  // inside the room, so the bounding box must only prefilter containment.
  PSLG graph;
  graph.vs = {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 25}, {50, 25}, {50, 75}, {0, 75}};
  std::vector<Cycle> const cycles{
      makeCycle({0, 1, 2, 3}, 20'000),
      makeCycle({4, 5, 6, 7}, 5'000)};

  // Hierarchy construction only reads minimal cycles; faces derive their
  // boundaries from the hierarchy alone.
  auto hierarchy = bw::core::arr::BuildPolygonHierarchy(graph, cycles);
  require(hierarchy[0].parent < 0,
          "the room should remain a root in the face hierarchy");
  require(hierarchy[0].children == std::vector<int>{1},
          "the equal-bound void should be a child of its room");
  require(hierarchy[1].parent == 0,
          "the void should not become an exterior inner boundary");

  auto faces = bw::core::arr::BuildFaces(hierarchy);
  require(faces[0].holes == std::vector<int>{1},
          "the room face should retain the void as its inner boundary");
}

}  // namespace

int main() {
  try {
    nestsAFlushVoidInsideItsRoom();
    std::cout << "Arrangement hierarchy accepts equal-bound containment\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
