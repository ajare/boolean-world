#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <core/LiquidProperties.h>

namespace {
using bw::core::CalculateLiquidExtinction;
using bw::core::GetLiquidProperties;
using bw::core::LiquidProperties;
using bw::core::LiquidType;

constexpr float Epsilon = 0.001f;

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

void waterOpticsAreAuthoredPerLiquidType() {
  auto const& water = GetLiquidProperties(LiquidType::Water);
  requireNear(water.opacity, 0.55f, "Water opacity changed unexpectedly");
  requireNear(water.referenceDepth, 40.0f,
              "Water reference depth changed unexpectedly");
  requireNear(water.tint[0], 0.10f, "Water red tint changed unexpectedly");
  requireNear(water.tint[1], 0.35f, "Water green tint changed unexpectedly");
  requireNear(water.tint[2], 0.60f, "Water blue tint changed unexpectedly");
}

void clearLiquidHasNoExtinction() {
  LiquidProperties liquid{1.0f, 1.0f, 0.0f, 10.0f, {0.1f, 0.5f, 0.9f}};
  for (auto coefficient : CalculateLiquidExtinction(liquid)) {
    requireNear(coefficient, 0.0f,
                "zero opacity must be perfectly clear in every channel");
  }
}

void fullyOpaqueLiquidStaysFinite() {
  LiquidProperties liquid{1.0f, 1.0f, 1.0f, 10.0f, {0.5f, 0.5f, 0.5f}};
  for (auto coefficient : CalculateLiquidExtinction(liquid)) {
    require(std::isfinite(coefficient),
            "fully opaque authored liquid must produce finite extinction");
    require(coefficient > 0.0f,
            "fully opaque authored liquid must still absorb light");
  }
}

void opacityMatchesTransmittanceAtReferenceDepth() {
  LiquidProperties liquid{1.0f, 1.0f, 0.5f, 8.0f, {0.5f, 0.5f, 0.5f}};
  for (auto coefficient : CalculateLiquidExtinction(liquid)) {
    requireNear(std::exp(-coefficient * liquid.referenceDepth),
                1.0f - liquid.opacity,
                "reference-depth transmittance must equal one minus opacity");
  }
}

void tintBiasesAbsorptionPerChannel() {
  LiquidProperties blue{1.0f, 1.0f, 0.5f, 8.0f, {0.1f, 0.35f, 0.6f}};
  auto blueExtinction = CalculateLiquidExtinction(blue);
  require(blueExtinction[0] > blueExtinction[2],
          "a blue tint must absorb red faster than blue");

  LiquidProperties grey{1.0f, 1.0f, 0.5f, 8.0f, {0.5f, 0.5f, 0.5f}};
  auto greyExtinction = CalculateLiquidExtinction(grey);
  requireNear(greyExtinction[0], greyExtinction[1],
              "a neutral tint must not bias red against green");
  requireNear(greyExtinction[1], greyExtinction[2],
              "a neutral tint must not bias green against blue");
}

}  // namespace

int main() {
  try {
    waterOpticsAreAuthoredPerLiquidType();
    clearLiquidHasNoExtinction();
    fullyOpaqueLiquidStaysFinite();
    opacityMatchesTransmittanceAtReferenceDepth();
    tintBiasesAbsorptionPerChannel();
    std::cout << "Liquid optical properties define finite, tint-biased extinction\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
