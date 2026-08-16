#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE {
namespace viz {
using namespace std;
using namespace wp;

RenderParams::RenderParams()
    : mRender(true), mRenderAccelerationGrids(false), mInvalidated(true) {
}

void RenderParams::setRender(bool render) {
  mRender = render;
}

bool RenderParams::getRender() const {
  return mRender;
}

bool RenderParams::isInvalidated() const {
  return mInvalidated;
}

void RenderParams::rebuilt() {
  mInvalidated = false;
}

void RenderParams::setRenderAccelerationGrids(bool render) {
  mRenderAccelerationGrids = render;
}

bool RenderParams::getRenderAccelerationGrids() const {
  return mRenderAccelerationGrids;
}

}  // namespace viz
}  // namespace WP_NAMESPACE
