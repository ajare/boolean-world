#include "core/WorldDataGenerator.h"
#include "core/World.h"
#include "core/Clipper.h"
#include "core/ClipperUtils.h"
#include "core/ClipperDefines.h"

namespace bw {
namespace core {
using namespace std;

WorldDataGenerator::WorldDataGenerator()
    : mCanClipPrimitives(true), mActiveLayer(0), mFlags(0), mViewTriangle{} {
}

WorldDataGenerator::~WorldDataGenerator() {
}

WorldDataGenerator::WorldDataGenerator(WorldDataGenerator const& other) {
  copyFrom(other);
}

WorldDataGenerator& WorldDataGenerator::operator=(WorldDataGenerator const& other) {
  copyFrom(other);
  return *this;
}

void WorldDataGenerator::copyFrom(WorldDataGenerator const& other) {
  mCanClipPrimitives = other.mCanClipPrimitives;
  mActiveLayer = other.mActiveLayer;
  mFlags = other.mFlags;
  mViewTriangle = other.mViewTriangle;
}

void WorldDataGenerator::setFlags(uint32_t flags) {
  mFlags = flags;
}

uint32_t WorldDataGenerator::getFlags() const {
  return mFlags;
}

bool WorldDataGenerator::flagSet(uint32_t flag) const {
  return (mFlags & flag) != 0;
}

vector<Primitive*> WorldDataGenerator::getPrimitives(World const* world, uint8_t layer) const {
  vector<Primitive*> primitives;

  for (auto primitive : world->getPrimitives()) {
    auto primitiveLayer = primitive->getLayer();

    // Ignore primitives on a different layer, unless either selection means all layers.
    if (primitiveLayer != layer && layer != BW_LAYER_ALL && primitiveLayer != BW_LAYER_ALL) {
      continue;
    }

    primitives.push_back(primitive);
  }

  return primitives;
}

WorldDataClipResults WorldDataGenerator::clipPrimitives(vector<Primitive*> const& primitives, World const* world, bool clipInputPrimitives) const {
  if (primitives.empty()) {
    return {};
  }

  // Clip Primitives together to form main mesh
  frame_number_type worldDataFrameNumber;
  Clipper borderClipper(world->getBorderVertexData(&worldDataFrameNumber), {},
                        world,
                        BW_CLIPPER_SET_PRIMITIVE |
                            BW_CLIPPER_GEN_INTER_ON_UNION);

  auto arrangementPolygons = borderClipper.clipToClipper2Polygons(primitives);
  auto const& vertexData = borderClipper.getClippedWorldVertexData();

  return {
      borderClipper.getBorderPolygons(),
      arrangementPolygons,
      vertexData,
      borderClipper.getArrangementGraph(),
      worldDataFrameNumber,
      borderClipper.getStats()};
}

bool WorldDataGenerator::canClipPrimitives() const {
  return mCanClipPrimitives;
}

void WorldDataGenerator::setActiveLayer(uint8_t layer) {
  mActiveLayer = layer;
}

uint8_t WorldDataGenerator::getActiveLayer() const {
  return mActiveLayer;
}

void WorldDataGenerator::handleEvents(uint32_t events) {
  BW_UNUSED(events);
}

void WorldDataGenerator::update(float frameTime, WorldUpdateData const& data, uint32_t events) {
  mCanClipPrimitives = !data.entityMoved && !data.entityTurned;
  mActiveLayer = data.activeLayer;

  auto halfFov = data.entityFov * 0.5f;
  auto viewDistance = data.entityViewDist * 1.1f / cosf(WP_DEGTORAD(halfFov));

  mViewTriangle = {
      data.entityPosition,
      data.entityPosition + wp::Vector2::fromAngle(data.entityAngle - halfFov, wp::Clockwise) * viewDistance,
      data.entityPosition + wp::Vector2::fromAngle(data.entityAngle + halfFov, wp::Clockwise) * viewDistance};

  handleEvents(events);
}

}  // namespace core
}  // namespace bw
