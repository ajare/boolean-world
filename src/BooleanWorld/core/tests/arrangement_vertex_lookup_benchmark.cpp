#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>

namespace {

using bw::core::arr::ArrangementResult;

std::shared_ptr<ArrangementResult> makeSparseArrangement(uint32_t dimension) {
  auto arrangement = std::make_shared<ArrangementResult>();
  arrangement->vertices.reserve(dimension * dimension);
  for (uint32_t y = 0; y < dimension; ++y) {
    for (uint32_t x = 0; x < dimension; ++x) {
      arrangement->vertices.push_back(
          {int64_t(x) * 1'000 + 500, int64_t(y) * 1'000 + 500});
    }
  }
  return arrangement;
}

std::vector<wp::Vector2> makeQueries(uint32_t dimension) {
  std::vector<wp::Vector2> queries;
  queries.reserve(dimension * dimension);
  for (uint32_t y = 0; y < dimension; ++y) {
    for (uint32_t x = 0; x < dimension; ++x) {
      queries.push_back({float(x) + 0.6f, float(y) + 0.6f});
    }
  }
  return queries;
}

int32_t findNearestVertexBefore(
    ArrangementResult const& arrangement,
    wp::Vector2 const& position,
    float radius) {
  int32_t result = -1;
  float nearest = radius;
  for (uint32_t i = 0; i < uint32_t(arrangement.vertices.size()); ++i) {
    auto const& vertex = arrangement.vertices[i];
    auto vertexPosition = wp::Vector2(
        bw::core::arr::ToWorldCoordinate(vertex.x),
        bw::core::arr::ToWorldCoordinate(vertex.y));
    auto distance = position.distanceTo(vertexPosition);
    if (distance <= nearest) {
      nearest = distance;
      result = int32_t(i);
    }
  }
  return result;
}

uint64_t checksum(std::vector<int32_t> const& results) {
  uint64_t result = 0;
  for (auto vertexIndex : results) {
    result = result * 131 + uint64_t(vertexIndex + 1);
  }
  return result;
}

template <typename Find>
double measure(
    Find&& find,
    std::vector<wp::Vector2> const& queries,
    int repetitions,
    uint64_t& resultChecksum) {
  std::vector<double> samples;
  samples.reserve(repetitions);
  std::vector<int32_t> results(queries.size());
  for (int i = 0; i < repetitions; ++i) {
    auto start = std::chrono::steady_clock::now();
    for (size_t queryIndex = 0; queryIndex < queries.size(); ++queryIndex) {
      results[queryIndex] = find(queries[queryIndex]);
    }
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
    resultChecksum ^= checksum(results);
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

}  // namespace

int main() {
  constexpr uint32_t dimension = 128;
  constexpr float radius = 0.25f;
  constexpr int repetitions = 5;
  auto arrangement = makeSparseArrangement(dimension);
  auto queries = makeQueries(dimension);
  bw::core::ArrangementWorldData worldData(
      arrangement,
      wp::BoundingBox(0.0f, 0.0f, float(dimension), float(dimension)),
      1.0f,
      0.0f);

  uint64_t beforeChecksum = 0;
  auto beforeMs = measure(
      [&](wp::Vector2 const& position) {
        return findNearestVertexBefore(*arrangement, position, radius);
      },
      queries,
      repetitions,
      beforeChecksum);

  uint64_t afterChecksum = 0;
  auto afterMs = measure(
      [&](wp::Vector2 const& position) {
        return worldData.getNearestVertexIndex(position, radius);
      },
      queries,
      repetitions,
      afterChecksum);

  if (beforeChecksum != afterChecksum) {
    std::cerr << "Vertex lookup checksum mismatch\n";
    return 1;
  }

  std::cout << "Arrangement nearest-vertex sparse benchmark: "
            << arrangement->vertices.size() << " vertices, "
            << queries.size() << " queries (median of " << repetitions << ")\n"
            << "  before linear scan: " << beforeMs << " ms\n"
            << "  after vertex-grid index: " << afterMs << " ms\n";
}
