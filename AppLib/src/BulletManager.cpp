#include <willpower/application/resourcesystem/AnimationSetResource.h>

#include "BulletManager.h"
#include "Game.h"
#include "ModelInstance.h"
#include "Exceptions.h"

namespace applib {
using namespace std;
using namespace wp;

BulletManager::BulletManager(application::resourcesystem::ResourcePtr resource, application::resourcesystem::ResourcePtr gameResource, firepower::MeshCollisionManager* meshCollisionMgr, size_t initialCapacity)
    : mBattery(initialCapacity, BulletManager::updateBullet), mResource(resource), mGameResource(gameResource), mRenderer(nullptr), mwMeshCollisionMgr(meshCollisionMgr) {
}

BulletManager::~BulletManager() {
  delete mRenderer;
}

void BulletManager::setupRenderer(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, mpp::ScenePtr scene, int renderOrder) {
  mDataProvider = make_shared<BulletRenderDataProvider>(&mBattery, mResource);

  uint32_t maxWidth, maxHeight;

  auto res = static_cast<application::resourcesystem::AnimationSetResource*>(mResource.get());
  auto texture = res->getImage()->getMppResource();
  res->getMaximumDimensions(maxWidth, maxHeight);

  viz::QuadRendererOptions renderOptions{
      viz::RotationOptions::TexCoordsByDirection,
      false,
      maxWidth,
      maxHeight,
      texture,
      16};

  mRenderer = new viz::DynamicQuadRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat>(
      "Bullets",
      renderOptions,
      mDataProvider,
      renderResourceMgr);

  mRenderer->build(renderSystem, renderResourceMgr);
  mRenderer->addToScene(scene, renderOrder);
}

bool BulletManager::destroyBullet(Bullet& bullet) {
  bullet.s_state &= ~BULLET_STATE_FIRING;

  if (bullet.v_explode_anim != ~0u) {
    bullet.s_state |= BULLET_STATE_EXPLODING;

    // Update position so that the explosion is rendered near the point of contact
    bullet.p_pos = bullet.p_pos + bullet.p_dir * bullet.p_radius;

    // Reset animation
    bullet.v_frame = 0;
    bullet.v_timer = 0;
    bullet.v_dir = 1;
    return true;
  } else {
    return false;
  }
}

bool BulletManager::updateBullet(Bullet& bullet, void* userObj, float frameTime) {
  auto mgr = static_cast<BulletManager*>(userObj);

  bullet.s_time += frameTime;

  // Visual
  auto animId = bullet.s_state & BULLET_STATE_FIRING ? bullet.v_fire_anim : bullet.v_explode_anim;
  auto const& anim = ModelInstance::animationDatabase()->getAnimation(animId);

  bullet.v_timer += frameTime;
  auto frame = ModelInstance::animationDatabase()->getAnimationFrame(animId, bullet.v_frame);

  if (anim.count > 0) {
    while (bullet.v_timer >= frame.time) {
      bullet.v_timer -= frame.time;
      bullet.v_frame += bullet.v_dir;

      // Check if we've hit the end of the animation
      if (bullet.v_frame == 0 || bullet.v_frame == anim.count) {
        // If we're exploding, we want to bail here
        if (bullet.s_state & BULLET_STATE_EXPLODING) {
          return false;
        }

        switch (anim.style) {
          case AnimationDatabase::LoopStyle::Forwards:
            bullet.v_frame = 0;
            break;

          case AnimationDatabase::LoopStyle::Once:
            bullet.v_frame = anim.count - 1;
            break;

          case AnimationDatabase::LoopStyle::PingPong:
            bullet.v_dir *= -1;
            bullet.v_frame += bullet.v_dir;
            break;

          default:
            throw Exception("Unknown loop style.  Must be one of Forwards|Once|PingPoing.");
        }
      }

      // Get next frame details, in case we have skipped past the new frame entirely
      frame = ModelInstance::animationDatabase()->getAnimationFrame(animId, bullet.v_frame);
    }
  }

  // Check if we've hit anything and update accordingly
  if (bullet.s_state & BULLET_STATE_FIRING) {
    if (bullet.params.life >= 0.0f && bullet.s_time >= bullet.params.life) {
      return destroyBullet(bullet);
    }

    auto newPos = bullet.p_pos + bullet.p_dir * bullet.params.speed * frameTime;

    if (mgr->mwMeshCollisionMgr) {
      int hitEdge = mgr->mwMeshCollisionMgr->checkBullet(bullet.p_pos, newPos, bullet.p_radius, false);

      if (hitEdge >= 0) {
        if (bullet.params.flags & BULLET_HIT_EXPLODE) {
          return destroyBullet(bullet);
        } else if (bullet.params.flags & BULLET_HIT_REFLECT) {
          auto edgeNormal = mgr->mwMeshCollisionMgr->getCollisionEdge(hitEdge).n;
          bullet.p_dir -= edgeNormal * 2 * (bullet.p_dir.dot(edgeNormal));
        }
      } else {
        bullet.p_pos = newPos;
      }
    } else {
      bullet.p_pos = newPos;
    }
  }

  return true;
}

void BulletManager::addBullet(int type, BulletParams const& params, Vector2 const& pos, Vector2 dir) {
  // Need to access relevant fire animation id for the given type
  auto const& bulletData = static_cast<Game*>(mGameResource.get())->getBulletData(type);

  Bullet b{
      type,
      params,
      pos,
      dir,
      bulletData.size,
      0.0f,  // animation timer
      (uint32_t)bulletData.fireAnimationId,
      (uint32_t)bulletData.explodeAnimationId};

  mBattery.addObject(b);
}

void BulletManager::update(BoundingBox const& viewBounds, float frameTime) {
  mBattery.update(this, frameTime);

  mDataProvider->update(frameTime);
  mRenderer->update(viewBounds, frameTime);
}

}  // namespace applib