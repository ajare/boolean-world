#include <willpower/application/resourcesystem/ImageResource.h>
#include <willpower/application/resourcesystem/ImageSetResource.h>
#include <willpower/application/resourcesystem/AnimationSetResource.h>

#include <willpower/common/Globals.h>

#include "BulletRenderDataProvider.h"
#include "ModelInstance.h"

namespace applib {
using namespace std;
using namespace wp;

BulletRenderDataProvider::BulletRenderDataProvider(Battery<Bullet>* battery, application::resourcesystem::ResourcePtr visual)
    : mwBattery(battery), mVisual(visual) {
  setNumPrimitives(0);

  auto animationSetRes = static_cast<application::resourcesystem::AnimationSetResource*>(mVisual.get());
  auto imageSetRes = static_cast<application::resourcesystem::ImageSetResource*>(mVisual->getDependentResource("Image").get());
  auto imageRes = static_cast<application::resourcesystem::ImageResource*>(imageSetRes->getDependentResource("Image").get());

  uint32_t bulletWidth, bulletHeight, imageWidth, imageHeight;

  imageWidth = imageRes->getWidth();
  imageHeight = imageRes->getHeight();

  animationSetRes->getMaximumDimensions(bulletWidth, bulletHeight);
}

void BulletRenderDataProvider::getBounds(glm::vec3& bMin, glm::vec3& bMax) {
  bMin.x = bMin.y = bMin.z = -1e10f;
  bMax.x = bMax.y = bMax.z = 1e10f;
}

void BulletRenderDataProvider::position(uint32_t index, float& x, float& y) {
  auto const& bullet = mwBattery->getObject(index);

  x = bullet.p_pos.x;
  y = bullet.p_pos.y;
}

void BulletRenderDataProvider::angle(uint32_t index, float& angle) {
  WP_UNUSED(index);
  WP_UNUSED(angle);
}

void BulletRenderDataProvider::direction(uint32_t index, float& x, float& y) {
  auto const& bullet = mwBattery->getObject(index);

  x = bullet.p_dir.x;
  y = bullet.p_dir.y;
}

void BulletRenderDataProvider::textureAtlasTexcoords(uint32_t index, float& u0, float& v0, float& u1, float& v1) {
  auto const& bullet = mwBattery->getObject(index);
  auto animId = bullet.s_state & BULLET_STATE_FIRING ? bullet.v_fire_anim : bullet.v_explode_anim;

  auto const& frame = ModelInstance::animationDatabase()->getAnimationFrame(animId, bullet.v_frame);

  u0 = frame.u[0];
  v0 = frame.v[0];
  u1 = frame.u[1];
  v1 = frame.v[1];
}

void BulletRenderDataProvider::dimensions(uint32_t index, float& halfWidth, float& halfHeight) {
  WP_UNUSED(index);
  WP_UNUSED(halfWidth);
  WP_UNUSED(halfHeight);
}

mpp::Colour BulletRenderDataProvider::diffuse() {
  return mpp::Colour::White;
}

bool BulletRenderDataProvider::update(float frameTime) {
  WP_UNUSED(frameTime);

  setNumPrimitives(mwBattery->getCount());
  return true;
}

}  // namespace applib