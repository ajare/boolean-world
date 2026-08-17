#pragma once

#include <array>
#include <vector>

#include "core/LayerSelection.h"
#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/WorldData.h"
#include "core/WorldUpdateData.h"

namespace bw::core {
class World;

[[nodiscard]] BW_API std::vector<Primitive*> selectAndOrderPrimitives(
    World const& world, LayerSelection const& selection);

class WorldDataGenerator {
public:
  struct SortPrimitivesByPriority {
    bool operator()(Primitive const* a, Primitive const* b) const {
      return a->getPriority() < b->getPriority();
    }
  };

private:
  LayerSelection mLayerSelection{SelectLayer(0)};

protected:
  std::array<wp::Vector2, 3> mViewTriangle;

private:
  virtual void handleEvents(uint32_t events);
  virtual void handleLayerSelectionChanged();

protected:
  void copyFrom(WorldDataGenerator const& other);

public:
  WorldDataGenerator();
  virtual ~WorldDataGenerator();
  WorldDataGenerator(WorldDataGenerator const& other);
  WorldDataGenerator& operator=(WorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() = 0;
  virtual WorldDataPtr getWorldData(World const* world) = 0;

  void setLayerSelection(LayerSelection const& selection);
  [[nodiscard]] LayerSelection const& getLayerSelection() const;

  void setActiveLayer(uint8_t layer);
  void update(float frameTime, WorldUpdateData const& data, uint32_t events);
  virtual void generate(World const* world, bool regetPrimitives) = 0;
};

}  // namespace bw::core
