#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include <core/RectanglePolygon.h>
#include <core/World.h>

namespace {

using bw::core::Primitive;
using bw::core::RectanglePolygon;
using bw::core::World;

uint32_t findExactBefore(World const& world, wp::Vector2 const& position) {
  auto const& primitives = world.getPrimitives();
  for (uint32_t i = 0; i < uint32_t(primitives.size()); ++i) {
    auto const* primitive = primitives[i];
    if (primitive->getBounds().pointInside(position) &&
        primitive->triangulate(true).pointInside(position)) {
      return i;
    }
  }
  return ~0u;
}

uint64_t checksum(std::vector<uint32_t> const& results) {
  uint64_t value = 0;
  for (auto result : results) {
    value = value * 131 + uint64_t(result) + 1;
  }
  return value;
}

template <typename Find>
double measure(
    Find&& find,
    std::vector<wp::Vector2> const& queries,
    int repetitions,
    uint64_t& resultChecksum) {
  std::vector<double> samples;
  std::vector<uint32_t> results(queries.size());
  samples.reserve(repetitions);
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    auto const start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < queries.size(); ++i) {
      results[i] = find(queries[i]);
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
  constexpr uint32_t dimension = 48;
  constexpr uint32_t primitiveCount = dimension * dimension;
  constexpr int repetitions = 5;
  World world(1024.0f, 8.0f);
  std::vector<wp::Vector2> queries;
  queries.reserve(primitiveCount);

  for (uint32_t y = 0; y < dimension; ++y) {
    for (uint32_t x = 0; x < dimension; ++x) {
      wp::Vector2 const position{-470.0f + float(x) * 20.0f, -470.0f + float(y) * 20.0f};
      auto rectangle = new RectanglePolygon(
          Primitive::Operation::Union,
          Primitive::FillRule::NonZero,
          1.0f);
      rectangle->setPosition(position);
      rectangle->setSize(2.0f, 2.0f);
      world.addPrimitive(rectangle);
      queries.push_back(position);
    }
  }

  uint64_t beforeChecksum = 0;
  auto const beforeMs = measure(
      [&](wp::Vector2 const& position) { return findExactBefore(world, position); },
      queries,
      repetitions,
      beforeChecksum);

  uint64_t afterChecksum = 0;
  auto const afterMs = measure(
      [&](wp::Vector2 const& position) { return world.findPrimitiveIndex(position, true); },
      queries,
      repetitions,
      afterChecksum);

  if (beforeChecksum != afterChecksum) {
    std::cerr << "Exact primitive-picking checksum mismatch\n";
    return 1;
  }

  std::cout << "Exact primitive-picking sparse benchmark: " << primitiveCount
            << " primitives, " << queries.size() << " queries (median of "
            << repetitions << ")\n"
            << "  before linear scan and per-query triangulation: " << beforeMs << " ms\n"
            << "  after primitive grid and cached triangulations: " << afterMs << " ms\n";
}
