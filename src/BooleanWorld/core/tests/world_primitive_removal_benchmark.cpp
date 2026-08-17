#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

uint32_t countRetainedBefore(uint32_t primitiveCount, std::vector<uint32_t> const& indices) {
  uint32_t retained = 0;
  for (uint32_t i = 0; i < primitiveCount; ++i) {
    if (std::find(indices.begin(), indices.end(), i) == indices.end()) {
      retained++;
    }
  }
  return retained;
}

uint32_t countRetainedAfter(uint32_t primitiveCount, std::vector<uint32_t> indices) {
  std::sort(indices.begin(), indices.end());

  uint32_t retained = 0;
  uint32_t selectedIndex = 0;
  for (uint32_t i = 0; i < primitiveCount; ++i) {
    bool const selected = selectedIndex < indices.size() && indices[selectedIndex] == i;
    while (selectedIndex < indices.size() && indices[selectedIndex] <= i) {
      selectedIndex++;
    }
    if (!selected) {
      retained++;
    }
  }
  return retained;
}

template <typename Count>
double measure(Count&& count, int repetitions, uint32_t& resultChecksum) {
  std::vector<double> samples;
  samples.reserve(repetitions);
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    auto const start = std::chrono::steady_clock::now();
    resultChecksum ^= count();
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

}  // namespace

int main() {
  constexpr uint32_t primitiveCount = 20'000;
  constexpr int repetitions = 5;
  std::vector<uint32_t> indices(primitiveCount / 2);
  std::iota(indices.begin(), indices.end(), 0);
  std::reverse(indices.begin(), indices.end());

  uint32_t beforeChecksum = 0;
  auto const beforeMs = measure(
      [&] { return countRetainedBefore(primitiveCount, indices); },
      repetitions,
      beforeChecksum);

  uint32_t afterChecksum = 0;
  auto const afterMs = measure(
      [&] { return countRetainedAfter(primitiveCount, indices); },
      repetitions,
      afterChecksum);

  if (beforeChecksum != afterChecksum) {
    std::cerr << "Primitive-removal membership checksum mismatch\n";
    return 1;
  }

  std::cout << "Primitive-removal membership benchmark: " << primitiveCount
            << " primitives, " << indices.size() << " unordered selected indices (median of "
            << repetitions << ")\n"
            << "  before linear membership scan per primitive: " << beforeMs << " ms\n"
            << "  after sorted two-pointer sweep: " << afterMs << " ms\n";
}
