#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/Arrangement.h>
#include <core/ArrangementWorldData.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bw::core::ArrangementWorldData makeWorldData() {
  auto arrangement = std::make_shared<bw::core::arr::ArrangementResult>();
  arrangement->vertices = {
      {1'000, 1'000},
      {3'000, 1'000},
      {130'000, 5'000}};
  return {arrangement, wp::BoundingBox(0.0f, 0.0f, 10.0f, 10.0f), 1.0f, 0.0f};
}

void findsNearestArrangementVertices() {
  auto worldData = makeWorldData();

  require(worldData.getNearestVertexIndex({1.0f, 1.0f}, 0.0f) == 0,
          "the vertex grid did not find an exact arrangement vertex");
  require(worldData.getNearestVertexIndex({2.0f, 1.0f}, 1.0f) == 1,
          "equal-distance vertices should retain the former later-index tie break");
  require(worldData.getNearestVertexIndex({2.0f, 1.0f}, 0.99f) == -1,
          "vertices outside the search radius should not be returned");
  require(worldData.getNearestVertexIndex(
              {2.0f, 1.0f}, std::numeric_limits<float>::infinity()) == 1,
          "an infinite radius should retain the linear lookup result");
  require(worldData.getNearestVertexIndex(
              {2.0f, 1.0f}, std::numeric_limits<float>::quiet_NaN()) == -1,
          "a NaN radius should retain the linear lookup result");
  require(worldData.getNearestVertexIndex({130.0f, 5.0f}, 0.0f) == 2,
          "the vertex grid must preserve lookup outside the supplied world extents");
}

}  // namespace

int main() {
  try {
    findsNearestArrangementVertices();
    std::cout << "Arrangement vertex lookup is spatially indexed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
