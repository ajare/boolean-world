#pragma once

#include <willpower/collide/Simulation.h>
#include <willpower/collide/Collider.h>

#include <entt/entt.hpp>

#include "Platform.h"
#include "Entity.h"
#include "AnimationDatabase.h"
#include "BulletManager.h"
#include "BeamManager.h"
#include "ObjectArray.h"
#include "Weapon.h"
#include "PhysicalStats.h"
#include "VisualSprite.h"
#include "BeamEmitter.h"

namespace applib {

class APPLIB_API EntityHandler {
protected:
  BulletManager* mwBulletMgr;

  BeamManager* mwBeamMgr;

  BulletWeapon mBulletWeapon;

  wp::collide::Simulation* mwSimulation;

  wp::collide::Collider* mwPlayerCollider;

  // Input
  std::vector<std::string> mActiveInputStates;

  float mMouseScreenX, mMouseScreenY;

  float mMouseDeltaX, mMouseDeltaY;

  wp::Vector2 mMouseWorld;

  // Component registries
  entt::registry mComponentRegistry;

  // Prototype lookups
  std::map<std::string, entt::entity> mProtoIds;

  static const size_t MaxMappings = 4096;

private:
  virtual std::string getPrototypeName(int type) = 0;

  virtual void setupImpl(Entity* entity) = 0;

  virtual void destroyImpl(Entity* entity) = 0;

  virtual bool updateImpl(Entity* entity, bool inputControlled, float frameTime) = 0;

public:
  EntityHandler();

  virtual ~EntityHandler() = default;

  template <typename T>
  void registerProtoComponent(entt::entity id, T const& component) {
    mComponentRegistry.emplace<T>(id, component);
  }

  template <typename T>
  T const& getEntityComponent(Entity const& entity) const {
    return mComponentRegistry.get<T>(entity.mCompSysId);
  }

  template <typename T>
  T& getEntityComponent(Entity const& entity) {
    return mComponentRegistry.get<T>(entity.mCompSysId);
  }

  template <typename T>
  bool entityHasComponent(Entity const& entity) const {
    return mComponentRegistry.try_get<T>(entity.mCompSysId) != nullptr;
  }

  void getMouseScreenPosition(float* mouseX, float* mouseY) const;

  void copyEntityComponents(entt::entity from, entt::entity to);

  entt::entity registerPrototype(std::string const& protoName);

  void setBulletManager(BulletManager* bulletMgr);

  void setBeamManager(BeamManager* beamMgr);

  void setActiveInputStates(std::vector<std::string> const& states, float mouseScreenX, float mouseScreenY, float mouseDeltaX, float mouseDeltaY, wp::Vector2 const& mouseWorld);

  std::vector<std::string> const& getActiveInputStates() const;

  void setupCollisions(wp::collide::Simulation* simulation, wp::collide::Collider* collider);

  void setup(Entity* entity, int type, wp::Vector2 const& position, float angle);

  void destroy(Entity* entity);

  void fireBullet(Entity* entity, int type, BulletParams const& params, BulletFireParams const& fireParams);

  virtual bool update(Entity* entity, bool controlActive, float frameTime);
};

}  // namespace applib