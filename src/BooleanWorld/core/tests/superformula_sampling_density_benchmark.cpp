#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include <willpower/common/Globals.h>

#include <core/Arrangement.h>
#include <core/SuperformulaPolygon.h>

namespace {

using bw::core::Primitive;
using bw::core::SuperformulaPolygon;
using bw::core::arr::Contour;
using bw::core::arr::FixedPointVertex;

constexpr uint32_t SamplingBaseResolution = 64;
constexpr int Repetitions = 9;
float Values[] = {1.0f, 1.0f, 3.0f, 4.5f, 10.0f, 10.0f};

float legacyRadius(float angle) {
  return std::pow(
      std::pow(std::abs(std::cos(Values[2] * angle / 4.0f) / Values[0]), Values[4]) +
          std::pow(std::abs(std::sin(Values[2] * angle / 4.0f) / Values[1]), Values[5]),
      -1.0f / Values[3]);
}

Contour sampleLegacyContour() {
  Contour contour;
  float const increment = 1.0f / SamplingBaseResolution;
  for (float angle = 0.0f; angle < WP_TWOPI; angle += increment) {
    float const radius = legacyRadius(angle);
    contour.push_back({bw::core::arr::ToFixedPointCoordinate(radius * std::cos(angle)),
                       bw::core::arr::ToFixedPointCoordinate(radius * std::sin(angle))});
  }
  return contour;
}

Contour sampleCurrentContour() {
  SuperformulaPolygon superformula(
      Primitive::Operation::Union, Primitive::FillRule::NonZero, 1.0f, Values);
  auto const& vertices = superformula.getVertices().front().front();
  Contour contour;
  contour.reserve(vertices.size());
  for (auto const& vertex : vertices) {
    contour.push_back({bw::core::arr::ToFixedPointCoordinate(vertex.p.x),
                       bw::core::arr::ToFixedPointCoordinate(vertex.p.y)});
  }
  return contour;
}

uint64_t checksum(Contour const& contour) {
  uint64_t result = 0;
  for (FixedPointVertex const& vertex : contour) {
    result = result * 131 + static_cast<uint64_t>(vertex.x) +
             static_cast<uint64_t>(vertex.y);
  }
  return result;
}

template <typename Function>
double measure(Function&& function) {
  std::array<double, Repetitions> samples;
  for (double& sample : samples) {
    auto const start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < 100; ++iteration) {
      function();
    }
    sample = std::chrono::duration<double, std::micro>(
                 std::chrono::steady_clock::now() - start)
                 .count() /
             100;
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

}  // namespace

int main() {
  auto const legacyContour = sampleLegacyContour();
  auto const currentContour = sampleCurrentContour();
  auto const legacyChecksum = checksum(legacyContour);
  auto const currentChecksum = checksum(currentContour);
  auto const legacyGenerationUs = measure([&] { return checksum(sampleLegacyContour()); });
  auto const currentGenerationUs = measure([&] { return checksum(sampleCurrentContour()); });
  auto const legacyArrangementInputUs = measure([&] {
    return bw::core::arr::BuildPSLG({{legacyContour, 0}}).es.size();
  });
  auto const currentArrangementInputUs = measure([&] {
    return bw::core::arr::BuildPSLG({{currentContour, 0}}).es.size();
  });

  if (legacyChecksum == 0 || currentChecksum == 0) {
    std::cerr << "Superformula sampling checksum unexpectedly zero\n";
    return 1;
  }

  auto const reduction = 100.0 *
                         (1.0 - double(currentContour.size()) / legacyContour.size());
  std::cout << std::fixed << std::setprecision(2)
            << "Superformula sampling-density benchmark: resolution 1 (median of "
            << Repetitions << ", 100 iterations)\n"
            << "  legacy contour generation: " << legacyGenerationUs << " us, "
            << legacyContour.size() << " vertices fed to arrangement\n"
            << "  current contour generation: " << currentGenerationUs << " us, "
            << currentContour.size() << " vertices fed to arrangement\n"
            << "  arrangement input BuildPSLG: " << legacyArrangementInputUs << " us -> "
            << currentArrangementInputUs << " us\n"
            << "  arrangement-input reduction: " << reduction << "%\n";
}
