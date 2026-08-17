#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include <willpower/common/AccelerationGrid.h>

namespace {

using Clock = std::chrono::steady_clock;

struct Query {
  int x0;
  int y0;
  int x1;
  int y1;
};

template <typename Function>
std::pair<double, uint64_t> measure(Function function) {
  auto const start = Clock::now();
  auto const checksum = function();
  auto const elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  return {elapsed, checksum};
}

}  // namespace

int main() {
  constexpr int dimension = 128;
  constexpr int itemCount = 8192;
  constexpr int queryCount = 10000;
  constexpr int repetitions = 10;

  wp::AccelerationGrid grid(0.0f, 0.0f, float(dimension), float(dimension), dimension, dimension, 0.0f);
  std::mt19937 random(102);
  std::uniform_int_distribution<int> cell(0, dimension - 2);
  for (uint32_t item = 0; item < itemCount; ++item) {
    auto const x = cell(random);
    auto const y = cell(random);
    auto const span = item % 8 == 0 ? 1.25f : 0.25f;
    grid.addItem(item, wp::BoundingBox(float(x) + 0.1f, float(y) + 0.1f, span, span));
  }

  std::vector<std::set<uint32_t>> legacyCells(dimension * dimension);
  for (int y = 0; y < dimension; ++y) {
    for (int x = 0; x < dimension; ++x) {
      auto const& flatCell = grid._getCellItems(x, y);
      legacyCells[y * dimension + x].insert(flatCell.begin(), flatCell.end());
    }
  }

  std::vector<Query> queries;
  queries.reserve(queryCount);
  std::uniform_int_distribution<int> queryOrigin(0, dimension - 17);
  std::uniform_int_distribution<int> querySize(1, 16);
  for (int i = 0; i < queryCount; ++i) {
    auto const x0 = queryOrigin(random);
    auto const y0 = queryOrigin(random);
    queries.push_back({x0, y0, x0 + querySize(random), y0 + querySize(random)});
  }

  double legacyMilliseconds = 0.0;
  double flatMilliseconds = 0.0;
  uint64_t legacyChecksum = 0;
  uint64_t flatChecksum = 0;
  wp::AccelerationGrid::IndexCollection flatResult;

  for (int repetition = 0; repetition < repetitions; ++repetition) {
    auto const legacy = measure([&] {
      uint64_t checksum = 0;
      for (auto const& query : queries) {
        std::set<uint32_t> result;
        for (int y = query.y0; y <= query.y1; ++y) {
          for (int x = query.x0; x <= query.x1; ++x) {
            auto const& source = legacyCells[y * dimension + x];
            result.insert(source.begin(), source.end());
          }
        }
        checksum += result.size();
      }
      return checksum;
    });
    legacyMilliseconds += legacy.first;
    legacyChecksum = legacy.second;

    auto const flat = measure([&] {
      uint64_t checksum = 0;
      for (auto const& query : queries) {
        grid._getItemsInCellRange(query.x0, query.y0, query.x1, query.y1, flatResult);
        checksum += flatResult.size();
      }
      return checksum;
    });
    flatMilliseconds += flat.first;
    flatChecksum = flat.second;
  }

  if (legacyChecksum != flatChecksum) {
    std::cerr << "Result mismatch: legacy=" << legacyChecksum << ", flat=" << flatChecksum << '\n';
    return 1;
  }

  legacyMilliseconds /= repetitions;
  flatMilliseconds /= repetitions;
  std::cout << "128x128 cells, " << itemCount << " items, " << queryCount << " queries\n"
            << "legacy std::set cells/result: " << legacyMilliseconds << " ms\n"
            << "flat vector cells/reused result: " << flatMilliseconds << " ms\n"
            << "speedup: " << legacyMilliseconds / flatMilliseconds << "x\n"
            << "empty cell payload: set=" << sizeof(std::set<uint32_t>)
            << " bytes, vector=" << sizeof(std::vector<uint32_t>) << " bytes\n"
            << "checksum: " << flatChecksum << '\n';
  return 0;
}
