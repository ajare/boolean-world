#pragma once

#include <vector>

#include "core/Platform.h"
#include "core/WorldDataGenerator.h"
#include "core/Clipper.h"

namespace bw {
namespace core {

class DefaultWorldDataGenerator : public WorldDataGenerator {
  WorldData mWorldData;

public:
  DefaultWorldDataGenerator();

  ~DefaultWorldDataGenerator();

  DefaultWorldDataGenerator(DefaultWorldDataGenerator const& other);

  DefaultWorldDataGenerator& operator=(DefaultWorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() override;

  WorldData getWorldData(World const* world) override;

  void generate(World const* world, bool regetPrimitives) override;
};

}  // namespace core
}  // namespace bw
