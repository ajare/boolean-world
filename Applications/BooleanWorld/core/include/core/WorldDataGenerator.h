#pragma once

#include <array>
#include <functional>
#include <vector>

#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/WorldData.h"
#include "core/WorldUpdateData.h"

namespace bw::core {
class World;

class WorldDataGenerator {
public:
  struct SortPrimitivesByPriority {
    bool operator()(Primitive const* a, Primitive const* b) const {
      return a->getPriority() < b->getPriority();
    }
  };

private:
  uint8_t mActiveLayer{0};

protected:
  std::array<wp::Vector2, 3> mViewTriangle;

private:
  virtual void handleEvents(uint32_t events);

protected:
  void copyFrom(WorldDataGenerator const& other);
  std::vector<Primitive*> getPrimitives(
      World const* world,
      uint8_t layer) const;

public:
  WorldDataGenerator();
  virtual ~WorldDataGenerator();
  WorldDataGenerator(WorldDataGenerator const& other);
  WorldDataGenerator& operator=(WorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() = 0;
  virtual WorldDataPtr getWorldData(World const* world) = 0;

  void setActiveLayer(uint8_t layer);
  uint8_t getActiveLayer() const;
  void update(float frameTime, WorldUpdateData const& data, uint32_t events);
  virtual void generate(World const* world, bool regetPrimitives) = 0;
};

using WorldDataGeneratorFactory =
    std::function<WorldDataGenerator*(wp::Vector2, int, int, float)>;
}  // namespace bw::core
