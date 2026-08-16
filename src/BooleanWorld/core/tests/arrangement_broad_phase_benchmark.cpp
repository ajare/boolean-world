#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include <core/Arrangement.h>

namespace {

std::vector<bw::core::arr::ContourInput> makeSparseContours(uint32_t count) {
  using bw::core::arr::ContourInput;
  using bw::core::arr::FixedPointVertex;

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

}  // namespace

int main() {
  constexpr uint32_t contourCount = 2000;
  auto contours = makeSparseContours(contourCount);

  bw::core::arr::PSLGConstructionStats stats;
  auto start = std::chrono::steady_clock::now();
  auto graph = bw::core::arr::BuildPSLG(contours, &stats);
  auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start);

  std::cout << "BuildPSLG sparse benchmark: " << contourCount << " contours, "
            << contourCount * 4 << " segments, " << graph.vs.size()
            << " vertices, " << graph.es.size() << " edges: "
            << elapsed.count() << " ms\n"
            << "  exact segment-pair tests: "
            << stats.candidateSegmentPairTests << " / "
            << stats.exhaustiveSegmentPairTests << " exhaustive\n"
            << "  exact point-segment tests: "
            << stats.candidatePointSegmentTests << " / "
            << stats.exhaustivePointSegmentTests << " exhaustive\n";
}
