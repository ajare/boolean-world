#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include <willpower/common/AccelerationGrid.h>
#include <willpower/common/BoundingBox.h>

#include <core/ImmutableAccelerationGrid.h>

namespace {
using Bounds = bw::core::ImmutableAccelerationGrid::ItemBounds;
constexpr int Dimension = 128;
constexpr float WorldSize = float(Dimension);

std::vector<Bounds> makeBounds(uint32_t count, uint32_t salt, float extent) {
  std::vector<Bounds> result;
  result.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    auto x = float((index * 37 + salt * 17) % Dimension) + 0.1f;
    auto y = float((index * 53 + salt * 29) % Dimension) + 0.1f;
    result.push_back({{x, y}, {std::min(x + extent, WorldSize), std::min(y + extent, WorldSize)}});
  }
  return result;
}

size_t legacyStorageBytes(
    wp::AccelerationGrid const& grid,
    uint32_t itemCount) {
  // Includes cell-vector objects and an estimate for each standard-library map
  // node in addition to the capacities visible through the legacy API.
  size_t bytes = sizeof(grid) +
                 size_t(Dimension) * Dimension * sizeof(std::vector<uint32_t>) +
                 size_t(itemCount) *
                     (sizeof(std::pair<uint32_t const, std::vector<uint32_t>>) +
                      4 * sizeof(void*));
  for (int y = 0; y < Dimension; ++y) {
    for (int x = 0; x < Dimension; ++x) {
      bytes += grid._getCellItems(x, y).capacity() * sizeof(uint32_t);
    }
  }
  for (uint32_t index = 0; index < itemCount; ++index) {
    bytes += grid._getItemCellIndices(index).capacity() * sizeof(uint32_t);
  }
  return bytes;
}

struct LegacyResult {
  std::vector<std::unique_ptr<wp::AccelerationGrid>> grids;
};

LegacyResult buildLegacy(std::vector<std::vector<Bounds>> const& inputs) {
  LegacyResult result;
  for (auto const& bounds : inputs) {
    auto grid = std::make_unique<wp::AccelerationGrid>(
        0.0f, 0.0f, WorldSize, WorldSize, Dimension, Dimension, 0.0f);
    for (uint32_t index = 0; index < uint32_t(bounds.size()); ++index) {
      auto const& item = bounds[index];
      grid->addItem(
          index,
          wp::BoundingBox(
              item.minExtent, item.maxExtent - item.minExtent));
    }
    result.grids.push_back(std::move(grid));
  }
  return result;
}

struct ImmutableResult {
  std::vector<std::unique_ptr<bw::core::ImmutableAccelerationGrid>> grids;
};

ImmutableResult buildImmutable(std::vector<std::vector<Bounds>> const& inputs) {
  ImmutableResult result;
  for (auto const& bounds : inputs) {
    auto grid = std::make_unique<bw::core::ImmutableAccelerationGrid>(
        wp::Vector2{0.0f, 0.0f}, wp::Vector2{WorldSize, WorldSize},
        Dimension, Dimension, bounds);
    result.grids.push_back(std::move(grid));
  }
  return result;
}

uint64_t checksum(LegacyResult const& result) {
  uint64_t checksum = 0;
  for (auto const& grid : result.grids) {
    for (int y = 0; y < Dimension; ++y) {
      for (int x = 0; x < Dimension; ++x) {
        for (auto item : grid->_getCellItems(x, y)) {
          checksum = checksum * 131 + item + 1;
        }
      }
    }
  }
  return checksum;
}

uint64_t checksum(ImmutableResult const& result) {
  uint64_t checksum = 0;
  for (auto const& grid : result.grids) {
    for (int y = 0; y < Dimension; ++y) {
      for (int x = 0; x < Dimension; ++x) {
        for (auto item : grid->getCellItems(x, y)) {
          checksum = checksum * 131 + item + 1;
        }
      }
    }
  }
  return checksum;
}

template <typename Build>
auto measure(Build&& build, int repetitions, double& medianMs) {
  std::vector<double> samples;
  for (int repetition = 0; repetition < repetitions - 1; ++repetition) {
    auto start = std::chrono::steady_clock::now();
    auto result = build();
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
  }
  auto start = std::chrono::steady_clock::now();
  auto result = build();
  samples.push_back(std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count());
  std::sort(samples.begin(), samples.end());
  medianMs = samples[samples.size() / 2];
  return result;
}
}  // namespace

int main() {
  constexpr int repetitions = 5;
  std::vector<std::vector<Bounds>> inputs;
  inputs.push_back(makeBounds(32'768, 1, 1.8f));  // triangles
  inputs.push_back(makeBounds(24'576, 2, 0.0f));  // vertices
  inputs.push_back(makeBounds(16'384, 3, 2.7f));  // walls

  double beforeMs = 0.0;
  auto before = measure([&] { return buildLegacy(inputs); }, repetitions, beforeMs);
  double afterMs = 0.0;
  auto after = measure(
      [&] { return buildImmutable(inputs); }, repetitions, afterMs);
  if (checksum(before) != checksum(after)) {
    std::cerr << "Bulk grid checksum mismatch\n";
    return 1;
  }

  size_t beforeBytes = 0;
  for (size_t index = 0; index < before.grids.size(); ++index) {
    beforeBytes += legacyStorageBytes(
        *before.grids[index], uint32_t(inputs[index].size()));
  }
  size_t afterBytes = 0;
  for (auto const& grid : after.grids) {
    afterBytes += sizeof(*grid) + grid->storageBytes();
  }

  constexpr double mib = 1024.0 * 1024.0;
  std::cout << std::fixed << std::setprecision(2)
            << "Arrangement query-grid generation benchmark: "
            << inputs[0].size() << " triangles, " << inputs[1].size()
            << " vertices, " << inputs[2].size() << " walls (median of "
            << repetitions << ")\n"
            << "  before incremental mutable grids: " << beforeMs
            << " ms, estimated retained grid memory " << beforeBytes / mib
            << " MiB\n"
            << "  after bulk immutable grids: " << afterMs
            << " ms, estimated retained grid memory " << afterBytes / mib
            << " MiB\n";
}
