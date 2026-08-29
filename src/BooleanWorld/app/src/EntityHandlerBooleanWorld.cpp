#include <cmath>
#include <numbers>

#include <willpower/common/Vector2.h>

#include <core/Utils.h>

#include <common/GameDefines.h>

#include "EntityHandlerBooleanWorld.h"
#include "PlayerView.h"
#include "EntityType.h"

#include "GameException.h"
#include "EntityProperties.h"

using namespace std;
using namespace wp;
using namespace applib;

map<EntityType, string> gEntityTypeNames = {
    {EntityType::Player, "Player"}};

EntityHandlerBooleanWorld::EntityHandlerBooleanWorld(shared_ptr<AnimationDatabase> animationDatabase, bw::app::InputOptions const& inputOptions)
    : EntityHandler(), mAnimationDatabase(animationDatabase), mInputEnabled(true), mInputOptions(inputOptions) {
}

bw::app::InputOptions const& EntityHandlerBooleanWorld::getInputOptions() const {
  return mInputOptions;
}

void EntityHandlerBooleanWorld::setInputOptions(bw::app::InputOptions const& inputOptions) {
  mInputOptions = inputOptions;
}

void EntityHandlerBooleanWorld::setSpeedMultiplier(float multiplier) {
  mSpeedMultiplier = multiplier;
}

void EntityHandlerBooleanWorld::setSwimming(bool swimming) {
  mSwimming = swimming;
}

void EntityHandlerBooleanWorld::enableInput(bool enable) {
  mInputEnabled = enable;
}

bool EntityHandlerBooleanWorld::isInputEnabled() const {
  return mInputEnabled;
}

string EntityHandlerBooleanWorld::getPrototypeName(int type) {
  auto it = gEntityTypeNames.find((EntityType)type);

  if (it != gEntityTypeNames.end()) {
    return it->second;
  } else {
    throw GameException(STR_FORMAT("Unknown entity type: {}", type));
  }
}

void EntityHandlerBooleanWorld::setupImpl(Entity* entity) {
  VAR_UNUSED(entity);
}

void EntityHandlerBooleanWorld::destroyImpl(Entity* entity) {
  VAR_UNUSED(entity);
}

bool EntityHandlerBooleanWorld::updateImpl(Entity* entity, bool inputControlled, float frameTime) {
  if (inputControlled) {
    if (mInputEnabled) {
      wp::Vector2 curPosition, newPosition, velocity;
      float curAngle, newAngle, curPitch, newPitch, verticalEffort;

      // Vertical (swimming) movement is applied separately by
      // StatePlayBooleanWorld's own peekInput call, against physicalStats.floorZ
      // rather than this collider - so the vertical effort here is unused.
      peekInput(*entity, &curPosition, &newPosition, &curAngle, &newAngle, &curPitch, &newPitch, &velocity, &verticalEffort, frameTime);

      mwPlayerCollider->setMovement(velocity);
      mwSimulation->update(frameTime);

      auto& physicalStats = getEntityComponent<PhysicalStats>(*entity);

      physicalStats.position = mwPlayerCollider->getCentre();
      physicalStats.angle = newAngle;
      physicalStats.pitch = newPitch;
    }
  } else {
    auto entityType = entity->getType();

    switch (entityType) {
      case (int)EntityType::Player:
        break;

      default:
        throw GameException(STR_FORMAT("Unhandled entity type: {}", (int)entityType));
    }
  }

  return true;
}

void EntityHandlerBooleanWorld::updateVisual(Entity* entity, float frameTime) {
  auto visual = mComponentRegistry.try_get<VisualSprite>(entity->mCompSysId);
  if (!visual) {
    return;
  }

  auto const& anim = mAnimationDatabase->getAnimation((uint32_t)visual->animation);
  auto frame = mAnimationDatabase->getAnimationFrame((uint32_t)visual->animation, visual->frame);

  visual->timer += frameTime;
  while (visual->timer >= frame.time) {
    if (frame.time <= 0.0f) {
      break;
    }

    visual->timer -= frame.time;
    visual->frame += visual->direction;

    // Check if we've hit the end of the animation
    if (visual->frame == 0 || visual->frame == anim.count) {
      switch (anim.style) {
        case AnimationDatabase::LoopStyle::Forwards:
          visual->frame = 0;
          break;

        case AnimationDatabase::LoopStyle::Once:
          visual->frame = anim.count - 1;
          break;

        case AnimationDatabase::LoopStyle::PingPong:
          visual->direction *= -1;
          visual->frame += visual->direction;
          break;

        default:
          throw Exception("Unknown loop style.  Must be one of Forwards|Once|PingPoing.");
      }
    }

    // Get next frame details, in case we have skipped past the new frame entirely
    frame = mAnimationDatabase->getAnimationFrame((uint32_t)visual->animation, visual->frame);
  }
}

void EntityHandlerBooleanWorld::peekInput(applib::Entity const& entity, wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float* curPitch, float* newPitch, wp::Vector2* velocity, float* verticalEffort, float frameTime) const {
  auto vel = Vector2::ZERO;
  float playerSpeed = BW_PLAYER_SPEED * mSpeedMultiplier;

  for (auto const& state : mActiveInputStates) {
    if (state == "Up") {
      vel.y += 1.0f;
    } else if (state == "Down") {
      vel.y -= 1.0f;
    } else if (state == "Left") {
      vel.x -= 1.0f;
    } else if (state == "Right") {
      vel.x += 1.0f;
    }
  }

  auto const& physicalStats = getEntityComponent<applib::PhysicalStats>(entity);

  // Get desired direction. Mouse sensitivity scales view control here, where
  // the player is being turned in the 3d world, and nowhere else.
  *curAngle = physicalStats.angle;
  *newAngle = bw::app::applyMouseYaw(physicalStats.angle, mMouseDeltaX, mInputOptions.mouseSensitivity);

  *curPitch = physicalStats.pitch;
  *newPitch = bw::app::applyMousePitch(physicalStats.pitch, mMouseDeltaY, mInputOptions.mouseSensitivity);

  // Get desired movement
  vel.normalise();

  *verticalEffort = 0.0f;
  if (mSwimming) {
    // Fly controls: forward/back also rises/dives based on view pitch. This
    // is additive on top of full horizontal speed, not reallocated from it -
    // otherwise looking straight down/up while swimming (exactly what a
    // player does diving into water) would collapse cos(pitch) toward zero
    // and leave forward/back input with almost no horizontal effect at all,
    // making the player feel stuck in place vertically. Pitch is degrees,
    // positive looking down (see FpsCamera::updateAngles), so the view
    // direction's vertical component is -sin(pitch); strafing stays level,
    // matching a conventional fly camera.
    //
    // Reported as bare effort, not a speed: what it achieves depends on the
    // liquid being swum through, which is StatePlayBooleanWorld's business,
    // not this handler's. It is also untouched by mSpeedMultiplier, which
    // models buoyancy stealing the traction between feet and floor and so has
    // no bearing on a swimmer kicking up or down.
    auto pitchRadians = *newPitch * (std::numbers::pi_v<float> / 180.0f);
    *verticalEffort = -vel.y * std::sin(pitchRadians);
  }

  vel = bw::app::playerMovement(vel, *newAngle);
  vel *= playerSpeed;

  *curPosition = physicalStats.position;
  *newPosition = *curPosition + vel * frameTime;
  *velocity = vel;
}

bool EntityHandlerBooleanWorld::update(Entity* entity, bool controlActive, float frameTime) {
  // Update logic
  bool inputControlled = controlActive && (entity->getId() == 0);
  bool alive = updateImpl(entity, inputControlled, frameTime);

  if (alive) {
    // Update visual
    updateVisual(entity, frameTime);
  }

  return alive;
}
