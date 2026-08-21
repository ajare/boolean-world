#include <iostream>
#include <stdexcept>

#include "PrefabTilingGuide.h"

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void squareGuideUsesSizeAsItsEdgeLengthAndIsCentredOnTheOrigin() {
  auto const outline = editor::prefabTilingOutline(
      bw::core::PrefabTilingType::Square, 64.0f);

  require(outline.size() == 4, "a square tiling guide did not have four vertices");
  require(outline[0] == wp::Vector2{-32.0f, -32.0f} &&
              outline[1] == wp::Vector2{32.0f, -32.0f} &&
              outline[2] == wp::Vector2{32.0f, 32.0f} &&
              outline[3] == wp::Vector2{-32.0f, 32.0f},
          "a size-64 square tiling guide was not centred and bounded by +/-32");
}

}  // namespace

int main() {
  try {
    squareGuideUsesSizeAsItsEdgeLengthAndIsCentredOnTheOrigin();
    std::cout << "Prefab tiling guide tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
