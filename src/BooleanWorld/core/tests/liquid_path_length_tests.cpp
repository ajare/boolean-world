#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <core/LiquidProperties.h>

namespace {
using bw::core::CalculateLiquidPathLength;

constexpr float Epsilon = 0.001f;
constexpr float Dry = -std::numeric_limits<float>::infinity();

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon,
          message + " (expected " + std::to_string(expected) + ", got " +
              std::to_string(actual) + ")");
}

float path(
    std::array<float, 3> eye,
    std::array<float, 3> point,
    float eyeSurface,
    float pointSurface) {
  return CalculateLiquidPathLength(eye, point, eyeSurface, pointSurface);
}

void eachEyeAndPointArrangementUsesTheSubmergedSegment() {
  requireNear(path({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, Dry, 5.0f),
              5.0f, "an eye above a submerged point should see to the surface");
  requireNear(path({0.0f, 1.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, 5.0f, 5.0f),
              2.0f, "two submerged ends should include their entire chord");
  requireNear(path({0.0f, 0.0f, 0.0f}, {0.0f, 10.0f, 0.0f}, 5.0f, Dry),
              5.0f, "a submerged eye looking at a dry point should reach the surface");
  requireNear(path({0.0f, 10.0f, 0.0f}, {0.0f, 20.0f, 0.0f}, Dry, Dry),
              0.0f, "two dry ends should have no liquid path");
}

void horizontalSightlinesAreWhollyWetOrDry() {
  requireNear(path({0.0f, 2.0f, 0.0f}, {100.0f, 2.0f, 0.0f}, 5.0f, 5.0f),
              100.0f, "a level underwater sightline should retain its full length");
  requireNear(path({0.0f, 8.0f, 0.0f}, {100.0f, 8.0f, 0.0f}, Dry, Dry),
              0.0f, "a level dry sightline should have no liquid path");
}

void aSubmergedEyeSuppliesTheGoverningSurfaceForDryPoints() {
  requireNear(path({0.0f, 0.0f, 0.0f}, {0.0f, 15.0f, 0.0f}, 10.0f, Dry),
              10.0f,
              "a dry point above the surface should still absorb from a submerged eye");
}

void thePointsOwnFaceDecidesWhetherItIsWet() {
  requireNear(path({0.0f, 0.0f, 0.0f}, {100.0f, 5.0f, 0.0f}, 10.0f, Dry),
              10.0f,
              "a dry point below the eye's surface must not be treated as wet");
}

void aDryFarEndIsBoundedByTheEyesDepth() {
  auto near = path({0.0f, 0.0f, 0.0f}, {3.0f, 8.0f, 0.0f}, 5.0f, Dry);
  auto far = path({0.0f, 0.0f, 0.0f}, {30.0f, 8.0f, 0.0f}, 5.0f, Dry);
  requireNear(near, 5.0f, "a dry far end should be bounded by the eye depth");
  requireNear(far, near,
              "a dry far end should not gain liquid path as it recedes");
}

void crossingTheWaterlineKeepsSubmergedPathsContinuous() {
  auto fromEyeSurface = path(
      {0.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 5.0f, 5.0f);
  auto fromPointSurface = path(
      {0.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, Dry, 5.0f);
  requireNear(fromEyeSurface, fromPointSurface,
              "matching eye and point surfaces should meet continuously at the waterline");
}

}  // namespace

int main() {
  try {
    eachEyeAndPointArrangementUsesTheSubmergedSegment();
    horizontalSightlinesAreWhollyWetOrDry();
    aSubmergedEyeSuppliesTheGoverningSurfaceForDryPoints();
    thePointsOwnFaceDecidesWhetherItIsWet();
    aDryFarEndIsBoundedByTheEyesDepth();
    crossingTheWaterlineKeepsSubmergedPathsContinuous();
    std::cout << "CalculateLiquidPathLength applies the liquid absorption path rules\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
