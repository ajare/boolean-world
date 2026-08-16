#include "core/DefaultWorldDataGenerator.h"
#include "core/World.h"
#include "core/Clipper.h"
#include "core/ClipperUtils.h"

namespace bw {
namespace core {
using namespace std;

DefaultWorldDataGenerator::DefaultWorldDataGenerator()
    : WorldDataGenerator() {
}

DefaultWorldDataGenerator::~DefaultWorldDataGenerator() {
}

DefaultWorldDataGenerator::DefaultWorldDataGenerator(DefaultWorldDataGenerator const& other) {
  WorldDataGenerator::copyFrom(other);
}

DefaultWorldDataGenerator& DefaultWorldDataGenerator::operator=(DefaultWorldDataGenerator const& other) {
  WorldDataGenerator::copyFrom(other);
  return *this;
}

WorldDataGenerator* DefaultWorldDataGenerator::copy() {
  return new DefaultWorldDataGenerator(*this);
}

WorldData DefaultWorldDataGenerator::getWorldData(World const* world) {
  generate(world, false);

  return mWorldData;
}

void DefaultWorldDataGenerator::generate(World const* world, bool regetPrimitives) {
  BW_UNUSED(regetPrimitives);

  PrimitiveProcessingStats primStats;

  auto primitives = getPrimitives(world, getActiveLayer());

  sort(primitives.begin(), primitives.end(), SortPrimitivesByPriority());

  auto clipResults = clipPrimitives(primitives, world, true);

  // Set up data to return
  mWorldData = {
      world->getExtents(),
      (float)(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      ClipperUtils::convertClipper2PolygonsToClippedPolygons(clipResults.borderPolygons, nullptr),
      ClipperUtils::convertClipper2PolygonsToClippedPolygons(clipResults.borderPolygons, nullptr),
      clipResults.borderVertexData,
      clipResults.graph,
      clipResults.stats,
      primStats,
      world->getFrameNumber()};
}

}  // namespace core
}  // namespace bw
