#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::arr::Cycle;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::PolygonNode;
using bw::core::arr::PSLG;

struct Box {
  int64_t minx;
  int64_t miny;
  int64_t maxx;
  int64_t maxy;
};

std::pair<PSLG, std::vector<Cycle>> makeSparseCycles(int count) {
  PSLG graph;
  std::vector<Cycle> cycles;
  graph.vs.reserve(count * 4);
  cycles.reserve(count);
  for (int i = 0; i < count; ++i) {
    auto x = int64_t(i % 100) * 100;
    auto y = int64_t(i / 100) * 100;
    auto first = int(graph.vs.size());
    graph.vs.insert(
        graph.vs.end(),
        {{x, y}, {x + 10, y}, {x + 10, y + 10}, {x, y + 10}});
    cycles.push_back({{first, first + 1, first + 2, first + 3}, {}, 200});
  }
  return {std::move(graph), std::move(cycles)};
}

std::vector<PolygonNode> buildHierarchyBefore(
    PSLG const& graph,
    std::vector<Cycle> const& cycles) {
  std::vector<Box> boxes;
  boxes.reserve(cycles.size());
  for (auto const& cycle : cycles) {
    auto const& a = graph.vs[cycle.vis[0]];
    auto const& c = graph.vs[cycle.vis[2]];
    boxes.push_back({a.x, a.y, c.x, c.y});
  }

  std::vector<PolygonNode> nodes(cycles.size());
  for (int i = 0; i < int(cycles.size()); ++i) {
    nodes[i].cycleIndex = i;
    auto bestArea = std::numeric_limits<int64_t>::max();
    for (int j = 0; j < int(cycles.size()); ++j) {
      if (i == j) {
        continue;
      }
      auto const& outer = boxes[j];
      auto const& inner = boxes[i];
      if (outer.minx <= inner.minx && outer.maxx >= inner.maxx &&
          outer.miny <= inner.miny && outer.maxy >= inner.maxy &&
          std::abs(cycles[j].area) < bestArea) {
        // Every fixture cycle is an axis-aligned disjoint square. A bounding
        // box containment would therefore also pass the old point-in-cycle
        // test; none do in this sparse case.
        bestArea = std::abs(cycles[j].area);
        nodes[i].parent = j;
      }
    }
  }
  return nodes;
}

uint64_t checksum(std::vector<PolygonNode> const& nodes) {
  uint64_t result = 0;
  for (auto const& node : nodes) {
    result = result * 131 + uint64_t(node.parent + 1);
  }
  return result;
}

template <typename Build>
double measure(Build&& build, int repetitions, uint64_t& resultChecksum) {
  std::vector<double> samples;
  samples.reserve(repetitions);
  for (int i = 0; i < repetitions; ++i) {
    auto start = std::chrono::steady_clock::now();
    auto nodes = build();
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
    resultChecksum ^= checksum(nodes);
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

}  // namespace

int main() {
  constexpr int cycleCount = 8000;
  constexpr int repetitions = 5;
  auto [graph, cycles] = makeSparseCycles(cycleCount);

  uint64_t beforeChecksum = 0;
  auto beforeMs = measure(
      [&] { return buildHierarchyBefore(graph, cycles); },
      repetitions, beforeChecksum);

  bw::core::arr::PolygonHierarchyStats stats;
  uint64_t afterChecksum = 0;
  auto afterMs = measure(
      [&] { return bw::core::arr::BuildPolygonHierarchy(graph, cycles, &stats); },
      repetitions, afterChecksum);

  if (beforeChecksum != afterChecksum) {
    std::cerr << "Hierarchy checksum mismatch\n";
    return 1;
  }

  std::cout << "BuildPolygonHierarchy sparse benchmark: " << cycleCount
            << " cycles (median of " << repetitions << ")\n"
            << "  before linear scan: " << beforeMs << " ms\n"
            << "  after indexed candidates: " << afterMs << " ms\n"
            << "  candidate-box checks: "
            << stats.indexedCandidateBoxTests << " / "
            << stats.exhaustiveCandidateBoxTests << " exhaustive\n"
            << "  point-in-cycle tests: "
            << stats.pointInCycleTests << '\n';
}
