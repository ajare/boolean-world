#pragma once

#include <array>
#include <functional>
#include <map>

#include "core/WorldData.h"
#include "core/Primitive.h"
#include "core/Clipper2Polygon.h"
#include "core/WorldUpdateData.h"
#include "core/PolygonGraph.h"
#include "core/Stats.h"

namespace bw {
namespace core {
class World;

struct WorldDataClipResults {
  std::vector<Clipper2Polygon> borderPolygons;
  std::vector<Clipper2Polygon> arrangementPolygons;
  std::vector<WorldVertexData> borderVertexData;
  graph::PolygonGraph graph;
  frame_number_type vertexDataFrameNumber{0};
  ClipStats stats;
};

class WorldDataGenerator {
public:
  struct SortPrimitivesByPriority {
    bool operator()(Primitive const* a, Primitive const* b) {
      return a->getPriority() < b->getPriority();
    }
  };

private:
  bool mCanClipPrimitives;

  uint8_t mActiveLayer;

  uint32_t mFlags;

protected:
  std::array<wp::Vector2, 3> mViewTriangle;

private:
  virtual void handleEvents(uint32_t events);

protected:
  void copyFrom(WorldDataGenerator const& other);

  std::vector<Primitive*> getPrimitives(World const* world, uint8_t layer) const;

  WorldDataClipResults clipPrimitives(std::vector<Primitive*> const& primitives, World const* world, bool clipInputPrimitives) const;

  bool canClipPrimitives() const;

public:
  WorldDataGenerator();

  virtual ~WorldDataGenerator();

  WorldDataGenerator(WorldDataGenerator const& other);

  WorldDataGenerator& operator=(WorldDataGenerator const& other);

  virtual WorldDataGenerator* copy() = 0;

  virtual WorldDataPtr getWorldData(World const* world) = 0;

  void setFlags(uint32_t flags);

  uint32_t getFlags() const;

  bool flagSet(uint32_t flag) const;

  void setActiveLayer(uint8_t layer);

  uint8_t getActiveLayer() const;

  void update(float frameTime, WorldUpdateData const& data, uint32_t events);

  virtual void generate(World const* world, bool regetPrimitives) = 0;
};

typedef std::function<WorldDataGenerator*(wp::Vector2, int, int, float)> WorldDataGeneratorFactory;

}  // namespace core
}  // namespace bw
