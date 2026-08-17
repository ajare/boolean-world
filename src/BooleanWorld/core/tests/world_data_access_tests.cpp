#include <iostream>
#include <stdexcept>
#include <type_traits>

#include <core/World.h>

namespace {

using GetWorldData = bw::core::WorldDataPtr (bw::core::World::*)() const;

static_assert(
    std::is_same_v<decltype(&bw::core::World::getWorldData), GetWorldData>,
    "World data access must not accept obsolete view parameters");

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void worldDataAccessRequiresNoView() {
  bw::core::World world(100.0f, 10.0f);
  auto worldData = world.getWorldData();

  require(worldData != nullptr, "world data access did not return a snapshot");
}

}  // namespace

int main() {
  try {
    worldDataAccessRequiresNoView();
    std::cout << "World data access requires no view\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
