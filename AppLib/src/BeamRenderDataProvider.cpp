#include "BeamRenderDataProvider.h"

namespace applib {
using namespace std;
using namespace wp;

BeamRenderDataProvider::BeamRenderDataProvider(Battery<BeamInstance>* battery)
    : mwBattery(battery) {
  setNumPrimitives(0);
}

void BeamRenderDataProvider::getBounds(glm::vec3& bMin, glm::vec3& bMax) {
  bMin = mBounds[0];
  bMax = mBounds[1];
}

void BeamRenderDataProvider::position(uint32_t index, float& x0, float& y0, float& x1, float& y1) {
  auto const& line = mLines[index];

  x0 = line.v0.x;
  y0 = line.v0.y;
  x1 = line.v1.x;
  y1 = line.v1.y;
}

void BeamRenderDataProvider::colour(uint32_t index, uint8_t& red, uint8_t& green, uint8_t& blue, uint8_t& alpha) {
  auto const& line = mLines[index];

  red = line.r;
  green = line.g;
  blue = line.b;
  alpha = line.a;
}

mpp::Colour BeamRenderDataProvider::diffuse() {
  return mpp::Colour::White;
}

bool BeamRenderDataProvider::update(float frameTime) {
  WP_UNUSED(frameTime);

  setNumPrimitives(mLines.size());
  return true;
}

void BeamRenderDataProvider::clearData() {
  mLines.clear();

  mBounds[0] = glm::vec3(1e10f, 1e10f, 1e10f);
  mBounds[1] = glm::vec3(-1e10f, -1e10f, -1e10f);

  setNumPrimitives(0);
}

void BeamRenderDataProvider::addData(vector<firepower::BeamShard> const& shards) {
  for (auto const& shard : shards) {
    mLines.push_back({shard.nearVertex, shard.farVertex, 255, 0, 0, 255});

    // Near vertex bounds
    if (shard.nearVertex.x < mBounds[0].x) {
      mBounds[0].x = shard.nearVertex.x;
    }
    if (shard.nearVertex.y < mBounds[0].y) {
      mBounds[0].y = shard.nearVertex.y;
    }
    if (shard.nearVertex.x > mBounds[1].x) {
      mBounds[1].x = shard.nearVertex.x;
    }
    if (shard.nearVertex.y > mBounds[1].y) {
      mBounds[1].y = shard.nearVertex.y;
    }

    // Far vertex bounds
    if (shard.farVertex.x < mBounds[0].x) {
      mBounds[0].x = shard.farVertex.x;
    }
    if (shard.farVertex.y < mBounds[0].y) {
      mBounds[0].y = shard.farVertex.y;
    }
    if (shard.farVertex.x > mBounds[1].x) {
      mBounds[1].x = shard.farVertex.x;
    }
    if (shard.farVertex.y > mBounds[1].y) {
      mBounds[1].y = shard.farVertex.y;
    }
  }

  setNumPrimitives(getNumPrimitives() + shards.size());
}

}  // namespace applib