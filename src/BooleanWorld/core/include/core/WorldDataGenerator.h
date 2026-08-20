#pragma once

#include <array>
#include <functional>
#include <vector>

#include "core/LayerSelection.h"
#include "core/Platform.h"
#include "core/Primitive.h"
#include "core/WorldData.h"
#include "core/WorldUpdateData.h"

namespace bw::core {
class Layer;
class World;

// Generation-time visibility test. A Primitive the filter rejects is left out
// of the fold entirely, so it contributes no geometry at all - unlike a render
// -side skip, which only drops its overlay. The owning Layer comes with it
// because everything worth filtering on (build steps, Layer identity) is a
// Layer-level fact the Primitive itself cannot answer.
using PrimitiveFilter =
    std::function<bool(Layer const& layer, Primitive const* primitive)>;

// An empty filter admits every Primitive the selection owns.
[[nodiscard]] BW_API std::vector<Primitive*> selectAndOrderPrimitives(
    World const& world,
    LayerSelection const& selection,
    PrimitiveFilter const& filter = {});

class WorldDataGenerator {
public:
  struct SortPrimitivesByPriority {
    bool operator()(Primitive const* a, Primitive const* b) const {
      return a->getPriority() < b->getPriority();
    }
  };

private:
  LayerSelection mLayerSelection{SelectLayer(0)};

  PrimitiveFilter mPrimitiveFilter;

protected:
  std::array<wp::Vector2, 3> mViewTriangle;

private:
  virtual void handleEvents(uint32_t events);
  virtual void handleLayerSelectionChanged();
  virtual void handlePrimitiveFilterChanged();

protected:
  void copyFrom(WorldDataGenerator const& other);

public:
  WorldDataGenerator();
  virtual ~WorldDataGenerator();
  WorldDataGenerator(WorldDataGenerator const& other);
  WorldDataGenerator& operator=(WorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() = 0;
  virtual WorldDataGenerator* copyForWorld(World const* world);
  virtual void rebindToWorld(World const* world);
  virtual WorldDataPtr getWorldData(World const* world) = 0;

  void setLayerSelection(LayerSelection const& selection);
  [[nodiscard]] LayerSelection const& getLayerSelection() const;

  // A filter that reads live state answers freshly on every generation; call
  // refreshPrimitiveFilter when that state changes so a generator holding a
  // cached result knows to produce a new one.
  void setPrimitiveFilter(PrimitiveFilter filter);
  [[nodiscard]] PrimitiveFilter const& getPrimitiveFilter() const;
  void refreshPrimitiveFilter();

  void setActiveLayer(uint32_t layerId);

  // Assign without running the change hook: used when a World load re-homes
  // the selection onto its active Layer, where the caller drives the
  // regeneration itself.
  void _resetLayerSelection(LayerSelection const& selection);
  void update(float frameTime, WorldUpdateData const& data, uint32_t events);
  virtual void generate(World const* world, bool regetPrimitives) = 0;
};

}  // namespace bw::core
