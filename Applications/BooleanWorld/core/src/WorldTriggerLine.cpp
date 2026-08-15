#include <willpower/common/MathsUtils.h>

#include "core/WorldTriggerLine.h"

namespace bw {
namespace core {
using namespace std;

WorldTriggerLine::WorldTriggerLine()
    : WorldTriggerLine(0, {0, -1}, {0, 1}, WorldTriggerLineSide::Both) {
}

WorldTriggerLine::WorldTriggerLine(uint8_t layer, wp::Vector2 const& p0, wp::Vector2 const& p1, WorldTriggerLineSide side)
    : mId{~0u}, mLayer(layer), mTriggerCount{0, 0}, mPoints{p0, p1}, mSide(side) {
  updateBounds();
}

void WorldTriggerLine::setId(uint32_t id) {
  mId = id;
}

uint32_t WorldTriggerLine::getId() const {
  return mId;
}

void WorldTriggerLine::setLayer(uint8_t layer) {
  mLayer = layer;
}

uint8_t WorldTriggerLine::getLayer() const {
  return mLayer;
}

void WorldTriggerLine::setPoint(uint32_t index, wp::Vector2 const& position) {
  assert(index < 2);
  mPoints[index] = position;
}

wp::Vector2 const& WorldTriggerLine::getPoint(uint32_t index) const {
  assert(index < 2);
  return mPoints[index];
}

void WorldTriggerLine::setSide(WorldTriggerLineSide side) {
  mSide = side;
}

WorldTriggerLineSide WorldTriggerLine::getSide() const {
  return mSide;
}

uint32_t WorldTriggerLine::getTriggerCount(WorldTriggerLineSide side) const {
  return side == WorldTriggerLineSide::Both ? getTotalTriggerCount() : mTriggerCount[(uint32_t)side];
}

uint32_t WorldTriggerLine::getTotalTriggerCount() const {
  return mTriggerCount[0] + mTriggerCount[1];
}

void WorldTriggerLine::updateBounds() {
  const float pad = 1.0f;

  float minX = min(mPoints[0].x, mPoints[1].x);
  float minY = min(mPoints[0].y, mPoints[1].y);
  float maxX = max(mPoints[0].x, mPoints[1].x);
  float maxY = max(mPoints[0].y, mPoints[1].y);

  mBounds.setPosition(minX - pad, minY - pad);
  mBounds.setSize((maxX - minX) + pad * 2, (maxY - minY) + pad * 2);
}

wp::BoundingBox const& WorldTriggerLine::getBounds() const {
  return mBounds;
}

bool WorldTriggerLine::checkCollide(wp::Vector2 const& oldPos, wp::Vector2 const& newPos, float radius) {
  // Check side
  auto side = wp::MathsUtils::pointSideOnLine(oldPos, mPoints[0], mPoints[1]);

  switch (mSide) {
    case WorldTriggerLineSide::Red:
      if (side != wp::MathsUtils::Side::Left) {
        return false;
      }
      break;

    case WorldTriggerLineSide::Blue:
      if (side != wp::MathsUtils::Side::Right) {
        return false;
      }
      break;

    case WorldTriggerLineSide::Both:
    default:
      break;
  }

  // Extend points out by radius and do a simple line-line intersection
  auto dir = mPoints[1] - mPoints[0];
  dir.normalise();
  dir *= radius;

  if (wp::MathsUtils::lineIntersectsLine(oldPos, newPos, mPoints[0] - dir, mPoints[1] + dir)) {
    mTriggerCount[1 - (uint32_t)side]++;
    return true;
  } else {
    return false;
  }
}

bool WorldTriggerLine::childrenModified() const {
  return false;
}

void WorldTriggerLine::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("worldTriggerLine");
  {
    serializer->writeInt32("layer", (int32_t)mLayer);
    serializer->writeVector2("point0", mPoints[0]);
    serializer->writeVector2("point1", mPoints[1]);
    serializer->writeUint32("side", (uint32_t)mSide);

    serializer->endMap();  // worldTriggerLine
  }
}

bool WorldTriggerLine::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  uint8_t layer;
  wp::Vector2 point0, point1;
  WorldTriggerLineSide side;

  try {
    serializer->beginMap("worldTriggerLine");
    {
      layer = (uint8_t)serializer->readInt32("layer");
      point0 = serializer->readVector2("point0");
      point1 = serializer->readVector2("point1");
      side = (WorldTriggerLineSide)serializer->readUint32("side");

      serializer->endMap();  // worldTriggerLine
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mLayer = layer;
  mPoints[0] = point0;
  mPoints[1] = point1;
  mSide = side;
  mTriggerCount[0] = 0;
  mTriggerCount[1] = 0;

  updateBounds();

  return true;
}

}  // namespace core
}  // namespace bw