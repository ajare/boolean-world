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

void choosesTheSmallestIndexedContainingCycle() {
  PSLG graph;
  graph.vs = {
      {0, 0}, {300, 0}, {300, 300}, {0, 300}, {50, 50}, {250, 50}, {250, 250}, {50, 250}, {100, 100}, {150, 100}, {150, 150}, {100, 150}};
  std::vector<Cycle> const cycles{
      makeCycle({0, 1, 2, 3}, 180'000),
      makeCycle({4, 5, 6, 7}, 80'000),
      makeCycle({8, 9, 10, 11}, 5'000)};

  auto hierarchy = bw::core::arr::BuildPolygonHierarchy(graph, cycles);
  require(hierarchy[0].parent == -1, "the outer cycle should be a root");
  require(hierarchy[1].parent == 0, "the middle cycle should belong to the outer cycle");
  require(hierarchy[2].parent == 1,
          "the inner cycle should choose its smallest containing cycle");
}

void indexesSparseHierarchyCandidates() {
  constexpr int cycleCount = 400;
  PSLG graph;
  std::vector<Cycle> cycles;
  graph.vs.reserve(cycleCount * 4);
  cycles.reserve(cycleCount);
  for (int i = 0; i < cycleCount; ++i) {
    auto x = int64_t(i % 20) * 100;
    auto y = int64_t(i / 20) * 100;
    auto first = int(graph.vs.size());
    graph.vs.insert(
        graph.vs.end(),
        {{x, y}, {x + 10, y}, {x + 10, y + 10}, {x, y + 10}});
    cycles.push_back(makeCycle(
        {first, first + 1, first + 2, first + 3}, 200));
  }

  bw::core::arr::PolygonHierarchyStats stats;
  auto hierarchy = bw::core::arr::BuildPolygonHierarchy(graph, cycles, &stats);
  require(
      stats.indexedCandidateBoxTests * 20 < stats.exhaustiveCandidateBoxTests,
      "the hierarchy index did not reject sparse bounding boxes");
  require(stats.pointInCycleTests == 0,
          "disjoint cycles should not reach exact containment tests");
  for (auto const& node : hierarchy) {
    require(node.parent == -1, "a disjoint cycle acquired a parent");
  }
}

}  // namespace

int main() {
  try {
    nestsAFlushVoidInsideItsRoom();
    choosesTheSmallestIndexedContainingCycle();
    indexesSparseHierarchyCandidates();
    std::cout << "Arrangement hierarchy indexes containment candidates\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
