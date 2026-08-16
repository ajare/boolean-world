#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <core/CoreException.h>
#include <core/Interpolator.h>

namespace {

constexpr float Epsilon = 0.0001f;

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(float actual, float expected, std::string const& message) {
  require(std::abs(actual - expected) < Epsilon,
          message + ": expected " + std::to_string(expected) +
              ", got " + std::to_string(actual));
}

template <typename Function>
void requireCoreException(Function&& function, std::string const& message) {
  try {
    function();
  } catch (bw::core::CoreException const&) {
    return;
  }
  throw std::runtime_error(message);
}

void addingPointsPreservesSegmentStructure() {
  bw::core::Interpolator<float> interpolator;
  interpolator.setScale({0.0f, 0.0f}, {10.0f, 10.0f});
  interpolator.setPoints({{1.0f, 1.0f}, {9.0f, 9.0f}});

  interpolator.addPoint(10.0f, 10.0f);
  require(interpolator.getNumSegments() == interpolator.getNumPoints() - 1,
          "appending a point did not preserve segment structure");

  interpolator.addPoint(5.0f, 5.0f);
  require(interpolator.getNumSegments() == interpolator.getNumPoints() - 1,
          "inserting a middle point did not preserve segment structure");

  interpolator.addPoint(0.0f, 0.0f);
  require(interpolator.getNumSegments() == interpolator.getNumPoints() - 1,
          "inserting a first point did not preserve segment structure");

  auto const& points = interpolator.getPoints();
  require(points[0].first == 0.0f && points[1].first == 1.0f &&
              points[2].first == 5.0f && points[3].first == 9.0f &&
              points[4].first == 10.0f,
          "added points are not ordered by time");
}

void setScaleRejectsInvalidTimeRanges() {
  bw::core::Interpolator<float> interpolator(0.0f);

  requireCoreException(
      [&] { interpolator.setScale({-1.0f, 0.0f}, {1.0f, 1.0f}); },
      "setScale accepted a negative time minimum");
  requireCoreException(
      [&] { interpolator.setScale({1.0f, 0.0f}, {1.0f, 1.0f}); },
      "setScale accepted a degenerate time range");
  requireCoreException(
      [&] { interpolator.setScale({2.0f, 0.0f}, {1.0f, 1.0f}); },
      "setScale accepted a backwards time range");

  wp::Vector2 scaleMin;
  wp::Vector2 scaleMax;
  interpolator.getScale(&scaleMin, &scaleMax);
  require(scaleMin.x == 0.0f && scaleMax.x == 1.0f,
          "setScale changed the range after rejecting invalid input");
}

void setPointsRejectsEmptyStructure() {
  bw::core::Interpolator<float> interpolator;
  require(interpolator.getNumSegments() == 0,
          "an empty interpolator reports a non-zero segment count");

  requireCoreException(
      [&] { interpolator.setPoints({}); },
      "setPoints accepted an empty point list");
  require(interpolator.getNumPoints() == 0 && interpolator.getNumSegments() == 0,
          "setPoints changed the interpolator after rejecting an empty point list");
}

void getValueClampsAndTreatsZeroWidthSegmentsAsSteps() {
  bw::core::Interpolator<float> interpolator;
  interpolator.setScale({0.0f, 0.0f}, {10.0f, 10.0f});
  interpolator.setPoints(
      {{1.0f, 2.0f}, {5.0f, 4.0f}, {5.0f, 8.0f}, {9.0f, 10.0f}});

  requireNear(interpolator.getValue(0.0f), 2.0f,
              "below-range time did not clamp to the first point");
  requireNear(interpolator.getValue(10.0f), 10.0f,
              "above-range time did not clamp to the last point");
  requireNear(interpolator.getValue(5.0f), 8.0f,
              "a zero-width segment did not behave as a step");
}

}  // namespace

int main() {
  try {
    addingPointsPreservesSegmentStructure();
    setScaleRejectsInvalidTimeRanges();
    setPointsRejectsEmptyStructure();
    getValueClampsAndTreatsZeroWidthSegmentsAsSteps();
    std::cout << "Interpolator invariants passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
