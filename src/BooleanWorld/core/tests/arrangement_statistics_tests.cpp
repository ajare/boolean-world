#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void reportsArrangementDiagnostics() {
  using namespace bw::core;
  namespace arr = bw::core::arr;

  arr::ArrangementPrimitive primitive{
      {{{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}},
      Primitive::Operation::Union,
      Primitive::FillRule::NonZero,
      0,
      7,
      {}};
  ArrangementStats stats;
  auto arrangement = arr::BuildArrangement({primitive}, &stats);
  ArrangementWorldData worldData(
      arrangement,
      wp::BoundingBox(0.0f, 0.0f, 1.0f, 1.0f),
      1.0f,
      0.0f,
      &stats);

  require(stats.vertexCount == 4,
          "arrangement diagnostics did not report fixed-point vertices");
  require(stats.edgeCount == 4,
          "arrangement diagnostics did not report arrangement edges");
  require(stats.faceCount == 2,
          "arrangement diagnostics did not report exterior and solid faces");
  require(stats.triangleCount == 2,
          "arrangement diagnostics did not report solid-face triangles");
  require(stats.wallCount == 4,
          "arrangement diagnostics did not report border walls");
  require(stats.buildPSLGTimeNs + stats.classificationTimeNs > 0,
          "arrangement diagnostics did not record construction timings");
}

}  // namespace

int main() {
  try {
    reportsArrangementDiagnostics();
    std::cout << "Arrangement diagnostics report generated topology\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
