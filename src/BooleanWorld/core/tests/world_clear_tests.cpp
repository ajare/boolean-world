#include <iostream>
#include <stdexcept>

#include <core/World.h>
#include <core/WorldTriggerLine.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void clearReleasesTriggerLines() {
  bw::core::World world(100.0f, 10.0f);
  world.addTriggerLine(new bw::core::WorldTriggerLine(0, {10.0f, 20.0f}, {30.0f, 40.0f}));
  world.addTriggerLine(new bw::core::WorldTriggerLine(0, {50.0f, 60.0f}, {70.0f, 80.0f}));

  world.clear();

  require(world.getNumTriggerLines() == 0,
          "clearing a world did not release its trigger lines");

  world.createAccelerationGrids(10.0f);
  auto id = world.addTriggerLine(new bw::core::WorldTriggerLine(
      0, {10.0f, 20.0f}, {30.0f, 40.0f}));

  require(id == 0,
          "a trigger line added after clearing a world did not receive the first id");
  require(world.getNumTriggerLines() == 1,
          "a cleared world retained stale trigger lines");
}

}  // namespace

int main() {
  try {
    clearReleasesTriggerLines();
    std::cout << "World clear releases trigger lines\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
