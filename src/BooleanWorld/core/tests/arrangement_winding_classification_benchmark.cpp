#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include <core/Arrangement.h>

namespace {
using bw::core::Primitive;
using bw::core::arr::Membership;

struct Result {
  uint64_t checksum{0};
  uint64_t peakBytes{0};
};

bool member(int32_t winding, Primitive::FillRule fillRule) {
  return fillRule == Primitive::FillRule::EvenOdd
             ? (winding < 0 ? -winding : winding) % 2 == 1
             : winding != 0;
}

uint64_t checksumMemberships(std::vector<Membership> const& memberships,
                             uint32_t primitiveCount) {
  uint64_t checksum = 0;
  for (auto const& membership : memberships) {
    for (uint32_t primitiveIndex = 0;
         primitiveIndex < primitiveCount; ++primitiveIndex) {
      checksum = checksum * 131 + membership.contains(primitiveIndex);
    }
  }
  return checksum;
}

Result classifyBefore(uint32_t faceCount, uint32_t primitiveCount) {
  std::vector<std::vector<int32_t>> windings(
      faceCount, std::vector<int32_t>(primitiveCount));
  std::vector<Membership> memberships;
  memberships.reserve(faceCount);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    if (faceIndex > 0) {
      windings[faceIndex] = windings[faceIndex - 1];
      auto primitiveIndex = (faceIndex - 1) % primitiveCount;
      windings[faceIndex][primitiveIndex] +=
          faceIndex % 5 == 0 ? -1 : 1;
    }
    Membership membershipBits(primitiveCount);
    for (uint32_t primitiveIndex = 0;
         primitiveIndex < primitiveCount; ++primitiveIndex) {
      auto fillRule = primitiveIndex % 2 == 0
                          ? Primitive::FillRule::NonZero
                          : Primitive::FillRule::EvenOdd;
      membershipBits.set(
          primitiveIndex,
          member(windings[faceIndex][primitiveIndex], fillRule));
    }
    memberships.push_back(std::move(membershipBits));
  }
  auto membershipBytes =
      uint64_t(faceCount) * ((primitiveCount + 63) / 64) * sizeof(uint64_t);
  return {checksumMemberships(memberships, primitiveCount),
          uint64_t(faceCount) * primitiveCount * sizeof(int32_t) +
              membershipBytes};
}

Result classifyAfter(uint32_t faceCount, uint32_t primitiveCount) {
  std::vector<int32_t> windings(primitiveCount);
  std::vector<Membership> memberships;
  memberships.reserve(faceCount);
  for (uint32_t faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
    if (faceIndex > 0) {
      auto primitiveIndex = (faceIndex - 1) % primitiveCount;
      windings[primitiveIndex] += faceIndex % 5 == 0 ? -1 : 1;
    }
    Membership membershipBits(primitiveCount);
    for (uint32_t primitiveIndex = 0;
         primitiveIndex < primitiveCount; ++primitiveIndex) {
      auto fillRule = primitiveIndex % 2 == 0
                          ? Primitive::FillRule::NonZero
                          : Primitive::FillRule::EvenOdd;
      membershipBits.set(
          primitiveIndex, member(windings[primitiveIndex], fillRule));
    }
    memberships.push_back(std::move(membershipBits));
  }
  auto membershipBytes =
      uint64_t(faceCount) * ((primitiveCount + 63) / 64) * sizeof(uint64_t);
  // The production DFS also carries one small frame per face in its worst
  // case. Four machine words conservatively model that traversal stack.
  return {checksumMemberships(memberships, primitiveCount),
          uint64_t(primitiveCount) * sizeof(int32_t) + membershipBytes +
              uint64_t(faceCount) * 4 * sizeof(uint64_t)};
}

template <typename Classify>
std::pair<double, Result> measure(Classify&& classify, int repetitions) {
  std::vector<double> samples;
  Result result;
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    auto start = std::chrono::steady_clock::now();
    result = classify();
    samples.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count());
  }
  std::sort(samples.begin(), samples.end());
  return {samples[samples.size() / 2], result};
}

}  // namespace

int main() {
  constexpr uint32_t primitiveCount = 512;
  constexpr uint32_t faceCount = 16384;
  constexpr int repetitions = 5;
  auto [beforeMs, before] = measure(
      [&] { return classifyBefore(faceCount, primitiveCount); }, repetitions);
  auto [afterMs, after] = measure(
      [&] { return classifyAfter(faceCount, primitiveCount); }, repetitions);
  if (before.checksum != after.checksum) {
    std::cerr << "Winding classification checksum mismatch\n";
    return 1;
  }

  constexpr double mib = 1024.0 * 1024.0;
  std::cout << std::fixed << std::setprecision(2)
            << "Dense winding-classification benchmark: " << primitiveCount
            << " primitives, " << faceCount << " faces (median of "
            << repetitions << ")\n"
            << "  before dense rows/full-row copies: " << beforeMs
            << " ms, peak classification memory " << before.peakBytes / mib
            << " MiB\n"
            << "  after one mutable row/sparse transitions: " << afterMs
            << " ms, peak classification memory " << after.peakBytes / mib
            << " MiB\n";
}
