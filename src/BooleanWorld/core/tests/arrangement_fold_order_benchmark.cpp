#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::ArrangementPrimitive;
using bw::core::arr::Membership;
using bw::core::arr::PrimitiveFoldOrder;

bool apply(Primitive::Operation operation, bool accumulated, bool member) {
  switch (operation) {
    case Primitive::Operation::Union:
      return accumulated || member;
    case Primitive::Operation::Intersection:
      return accumulated && member;
    case Primitive::Operation::Difference:
      return accumulated && !member;
    case Primitive::Operation::XOR:
      return accumulated != member;
  }
  return accumulated;
}

std::vector<ArrangementPrimitive> makePrimitives(uint32_t count) {
  std::vector<ArrangementPrimitive> primitives;
  primitives.reserve(count);
  for (uint32_t primitiveIndex = 0; primitiveIndex < count; ++primitiveIndex) {
    primitives.push_back(
        {{},
         static_cast<Primitive::Operation>(primitiveIndex % 4),
         Primitive::FillRule::NonZero,
         uint8_t((primitiveIndex * 73) % 251),
         primitiveIndex});
  }
  return primitives;
}

std::vector<Membership> makeMemberships(
    uint32_t faceCount, uint32_t primitiveCount) {
  std::vector<Membership> memberships;
  memberships.reserve(faceCount);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    Membership membership(primitiveCount);
    for (uint32_t primitiveIndex = 0;
         primitiveIndex < primitiveCount; ++primitiveIndex) {
      membership.set(
          primitiveIndex, (faceIndex * 17 + primitiveIndex * 31) % 7 < 3);
    }
    memberships.push_back(std::move(membership));
  }
  return memberships;
}

bool evaluateFoldBefore(
    std::vector<ArrangementPrimitive> const& primitives,
    Membership const& membership) {
  std::vector<uint32_t> foldOrder(primitives.size());
  std::iota(foldOrder.begin(), foldOrder.end(), 0);
  std::stable_sort(
      foldOrder.begin(), foldOrder.end(), [&](uint32_t lhs, uint32_t rhs) {
        return primitives[lhs].priority < primitives[rhs].priority;
      });

  bool inside = false;
  for (auto primitiveIndex : foldOrder) {
    inside = apply(
        primitives[primitiveIndex].operation,
        inside,
        membership.contains(primitiveIndex));
  }
  return inside;
}

uint64_t classifyBefore(
    std::vector<ArrangementPrimitive> const& primitives,
    std::vector<Membership> const& memberships) {
  uint64_t result = 0;
  for (auto const& membership : memberships) {
    result = result * 131 + evaluateFoldBefore(primitives, membership);
  }
  return result;
}

uint64_t classifyAfter(
    std::vector<ArrangementPrimitive> const& primitives,
    std::vector<Membership> const& memberships,
    PrimitiveFoldOrder const& foldOrder) {
  uint64_t result = 0;
  for (auto const& membership : memberships) {
    result = result * 131 +
             bw::core::arr::EvaluateFold(primitives, membership, foldOrder);
  }
  return result;
}

template <typename Classify>
double measure(Classify&& classify, int repetitions, uint64_t& checksum) {
  std::vector<double> samples;
  samples.reserve(repetitions);
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    auto start = std::chrono::steady_clock::now();
    checksum ^= classify();
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

}  // namespace

int main() {
  constexpr uint32_t primitiveCount = 256;
  constexpr uint32_t faceCount = 4096;
  constexpr int repetitions = 5;
  auto primitives = makePrimitives(primitiveCount);
  auto memberships = makeMemberships(faceCount, primitiveCount);

  uint64_t beforeChecksum = 0;
  auto beforeMs = measure(
      [&] { return classifyBefore(primitives, memberships); },
      repetitions, beforeChecksum);

  auto foldOrder = bw::core::arr::BuildPrimitiveFoldOrder(primitives);
  uint64_t afterChecksum = 0;
  auto afterMs = measure(
      [&] { return classifyAfter(primitives, memberships, foldOrder); },
      repetitions, afterChecksum);

  if (beforeChecksum != afterChecksum) {
    std::cerr << "Fold classification checksum mismatch\n";
    return 1;
  }

  std::cout << "Arrangement fold-order benchmark: " << primitiveCount
            << " primitives, " << faceCount << " faces (median of "
            << repetitions << ")\n"
            << "  before per-face order: " << beforeMs << " ms ("
            << faceCount << " priority-order allocations and stable sorts)\n"
            << "  after precomputed order: " << afterMs
            << " ms (1 priority-order allocation and stable sort)\n";
}
