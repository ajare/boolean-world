#include <iostream>
#include <stdexcept>

#include <willpower/common/MathsUtils.h>

namespace {

template <typename T>
concept HasMutableEpsilon = requires {
  T::Epsilon = 0.0f;
};

static_assert(!HasMutableEpsilon<wp::MathsUtils>);
static_assert(wp::MathsUtils::Epsilon == 0.0001f);

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void nearParallelWorldScaleLinesDoNotIntersect() {
  auto const intersection = wp::MathsUtils::lineIntersectsLine(
      {0.0f, 0.0f}, {1000.0f, 0.0f}, {0.0f, -0.025f}, {1000.0f, 0.025f});

  require(intersection == wp::MathsUtils::LineIntersectionType::NotIntersecting,
          "near-parallel world-scale lines must not intersect");
}

}  // namespace

int main() {
  try {
    nearParallelWorldScaleLinesDoNotIntersect();
    std::cout << "Geometric epsilon passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
