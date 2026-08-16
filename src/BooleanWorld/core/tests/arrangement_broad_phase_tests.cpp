#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::arr::ContourInput;
using bw::core::arr::FixedPointVertex;
using bw::core::arr::PSLGConstructionStats;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<ContourInput> makeSparseContours(uint32_t count) {
  std::vector<ContourInput> contours;
  contours.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto x = int64_t(i % 50) * 10000;
    auto y = int64_t(i / 50) * 10000;
    contours.push_back({{{x, y}, {x + 1000, y}, {x + 1000, y + 1000}, {x, y + 1000}},
                        i});
  }
  return contours;
}

void rejectsMostSparsePairs() {
  constexpr uint32_t contourCount = 1000;
  auto contours = makeSparseContours(contourCount);
  PSLGConstructionStats stats;
  auto graph = bw::core::arr::BuildPSLG(contours, &stats);

  require(stats.segmentCount == contourCount * 4,
          "broad-phase fixture did not contain the expected segments");
  require(graph.vs.size() == contourCount * 4,
          "broad phase changed the vertices of disjoint contours");
  require(graph.es.size() == contourCount * 4,
          "broad phase changed the edges of disjoint contours");
  require(stats.candidateSegmentPairTests * 20 <
              stats.exhaustiveSegmentPairTests,
          "segment broad phase did not reject at least 95% of sparse pairs");
  require(stats.candidatePointSegmentTests * 20 <
              stats.exhaustivePointSegmentTests,
          "point broad phase did not reject at least 95% of sparse re-checks");
}

void preservesCrossingIntersections() {
  std::vector<ContourInput> contours{
      {{{0, 5}, {10, 5}}, 0},
      {{{5, 0}, {5, 10}}, 1}};
  PSLGConstructionStats stats;
  auto graph = bw::core::arr::BuildPSLG(contours, &stats);

  auto crossing = std::find(
      graph.vs.begin(), graph.vs.end(), FixedPointVertex{5, 5});
  require(crossing != graph.vs.end(),
          "grid broad phase missed an exact segment intersection");
  require(graph.vs.size() == 5,
          "crossing segments did not produce exactly one intersection vertex");
  require(graph.es.size() == 4,
          "crossing segments were not split into four arrangement edges");
  require(stats.candidateSegmentPairTests > 0,
          "crossing segments were not sent to the exact narrow phase");
}

}  // namespace

int main() {
  try {
    rejectsMostSparsePairs();
    preservesCrossingIntersections();
    std::cout << "Arrangement broad phase preserves topology and rejects sparse pairs\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
