#include <iostream>
#include <stdexcept>

#include <core/World.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TrackingWorldDataGenerator : public bw::core::WorldDataGenerator {
  int& mDestructionCount;

public:
  explicit TrackingWorldDataGenerator(int& destructionCount)
      : mDestructionCount(destructionCount) {
  }

  ~TrackingWorldDataGenerator() override {
    ++mDestructionCount;
  }

  bw::core::WorldDataGenerator* copy() override {
    return new TrackingWorldDataGenerator(mDestructionCount);
  }

  bw::core::WorldDataPtr getWorldData(bw::core::World const*) override {
    return nullptr;
  }

  void generate(bw::core::World const*, bool) override {
  }
};

void worldOwnsExplicitDataGenerator() {
  int destructionCount = 0;

  {
    bw::core::World world(100.0f, -1.0f);
    auto* first = new TrackingWorldDataGenerator(destructionCount);
    world.setWorldDataGenerator(first);

    require(world.getWorldDataGenerator() == first,
            "world did not retain its explicitly supplied data generator");

    auto* second = new TrackingWorldDataGenerator(destructionCount);
    world.setWorldDataGenerator(second);

    require(destructionCount == 1,
            "replacing a data generator did not destroy the previous owned generator");
    require(world.getWorldDataGenerator() == second,
            "world did not install the replacement data generator");
  }

  require(destructionCount == 2,
          "destroying a world did not destroy its owned data generator");
}

}  // namespace

int main() {
  try {
    worldOwnsExplicitDataGenerator();
    std::cout << "World owns explicit data generators\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
